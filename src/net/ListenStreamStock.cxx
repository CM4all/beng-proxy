// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "ListenStreamStock.hxx"
#include "spawn/Mount.hxx"
#include "spawn/MountNamespaceOptions.hxx"
#include "event/CoarseTimerEvent.hxx"
#include "event/SocketEvent.hxx"
#include "net/TempListener.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "util/Cancellable.hxx"
#include "util/DeleteDisposer.hxx"
#include "util/DisposablePointer.hxx"
#include "util/djb_hash.hxx"
#include "util/SharedLease.hxx"
#include "util/SpanCast.hxx"
#include "util/StringList.hxx"
#include "util/StringSplit.hxx"
#include "AllocatorPtr.hxx"
#include "stopwatch.hxx"

#include <string>

#include <sys/socket.h> // for SOCK_STREAM
#include <sys/stat.h> // for chmod()

class ListenStreamStock::Item final
	: public IntrusiveHashSetHook<IntrusiveHookMode::AUTO_UNLINK>,
	  public SharedAnchor,
	  ListenStreamReadyHandler
{
	const std::string key;

	ListenStreamStockHandler &handler;

	std::string tags;

	TempListener temp;

	SocketEvent socket;

	/**
	 * This re-enables polling the #socket after the child process
	 * has exited.
	 */
	CoarseTimerEvent rearm_timer;

	CoarseTimerEvent idle_timer;

	CancellablePointer start_cancel_ptr;

	std::exception_ptr error;

	DisposablePointer server;

	bool fade = false;

public:
	Item(EventLoop &event_loop,
	     std::string_view _key,
	     ListenStreamStockHandler &_handler)
		:key(_key), handler(_handler),
		 socket(event_loop, BIND_THIS_METHOD(OnSocketReady)),
		 rearm_timer(event_loop, BIND_THIS_METHOD(OnRearmTimer)),
		 idle_timer(event_loop, BIND_THIS_METHOD(OnIdleTimeout)) {

		auto s = temp.Create(SOCK_STREAM, 16);
		socket.Open(s.Release());
		socket.ScheduleRead();

		chmod(temp.GetPath(), 0666);
	}

	~Item() noexcept {
		if (start_cancel_ptr)
			start_cancel_ptr.Cancel();

		socket.Close();
	}

	std::string_view GetKey() const noexcept {
		return key;
	}

	bool IsTag(std::string_view tag) const noexcept {
		return StringListContains(tags, '\0', tag);
	}

	bool CanUse() const noexcept {
		return !fade;
	}

	void Fade() noexcept {
		fade = true;

		if (IsAbandoned()) {
			rearm_timer.Cancel();
			idle_timer.Schedule({});
		}
	}

	void Borrow() {
		if (error)
			std::rethrow_exception(error);

		idle_timer.Cancel();
	}

	const char *GetPath() const noexcept {
		return temp.GetPath();
	}

private:
	void OnSocketReady(unsigned events) noexcept;

	void OnRearmTimer() noexcept {
		assert(!idle_timer.IsPending());
		assert(!start_cancel_ptr);
		assert(!server);

		socket.ScheduleRead();
	}

	void OnIdleTimeout() noexcept {
		assert(!rearm_timer.IsPending());
		delete this;
	}

	// virtual methods from class SharedAnchor
	void OnAbandoned() noexcept override {
		if (fade || start_cancel_ptr || rearm_timer.IsPending()) {
			/* destroy immediately if we're in "fade" mode
			   or if we're currently waiting for server
			   startup (which means the client has given
			   up very quickly, and this process will
			   probably never be used again) */
			delete this;
			return;
		}

		/* keep the process around for some time */
		idle_timer.Schedule(std::chrono::minutes{5});
	}

	// virtual methods from class ListenStreamReadyHandler
	void OnListenStreamSuccess(DisposablePointer server,
				   std::string_view tags) noexcept override;
	void OnListenStreamError(std::exception_ptr error) noexcept override;
	void OnListenStreamExit() noexcept override;
};

inline void
ListenStreamStock::Item::OnSocketReady(unsigned) noexcept
{
	assert(!server);
	assert(!start_cancel_ptr);

	socket.Cancel();

	handler.OnListenStreamReady(key,
				    temp.GetPath(), socket.GetSocket(),
				    *this, start_cancel_ptr);
}

void
ListenStreamStock::Item::OnListenStreamSuccess(DisposablePointer _server,
					       std::string_view _tags) noexcept
{
	assert(start_cancel_ptr);
	assert(!server);

	start_cancel_ptr = {};

	server = std::move(_server);
	tags = _tags;
}

void
ListenStreamStock::Item::OnListenStreamError(std::exception_ptr _error) noexcept
{
	assert(start_cancel_ptr);
	assert(!server);

	start_cancel_ptr = {};

	// TODO log
	error = std::move(_error);
	Fade();
}

void
ListenStreamStock::Item::OnListenStreamExit() noexcept
{
	assert(!start_cancel_ptr);
	assert(server);

	server = {};

	if (IsAbandoned())
		Fade();
	else
		/* there's still somebody who needs the socket;
		   re-enable the SocketEvent, but only after some
		   backoff time to avoid a busy loop with a child
		   process that fails repeatedly */
		// TODO do we need to give up eventually?
		rearm_timer.Schedule(std::chrono::seconds{10});
}

inline std::size_t
ListenStreamStock::ItemHash::operator()(std::string_view key) const noexcept
{
	return djb_hash(AsBytes(key));
}

inline std::string_view
ListenStreamStock::ItemGetKey::operator()(const Item &item) const noexcept
{
	return item.GetKey();
}

ListenStreamStock::ListenStreamStock(EventLoop &_event_loop,
					       ListenStreamStockHandler &_handler) noexcept
	:event_loop(_event_loop), handler(_handler) {}

ListenStreamStock::~ListenStreamStock() noexcept
{
	items.clear_and_dispose(DeleteDisposer{});
}

void
ListenStreamStock::FadeAll() noexcept
{
	items.for_each([](auto &i){
		i.Fade();
	});
}

void
ListenStreamStock::FadeTag(std::string_view tag) noexcept
{
	items.for_each([tag](auto &i){
		if (i.IsTag(tag))
			i.Fade();
	});
}

std::pair<const char *, SharedLease>
ListenStreamStock::Get(std::string_view key)
{
	auto [it, inserted] = items.insert_check_if(key, [](const auto &i){ return i.CanUse(); });
	if (inserted) {
		auto *item = new Item(event_loop, key, handler);
		it = items.insert_commit(it, *item);
	} else {
		it->Borrow();
	}

	return {it->GetPath(), *it};
}

SharedLease
ListenStreamStock::Apply(AllocatorPtr alloc, MountNamespaceOptions &mount_ns)
{
	const auto key = ToStringView(mount_ns.mount_listen_stream);
	if (key.data() == nullptr)
		return {};

	auto [path, tag] = Split(key, '\0');
	if (path.empty())
		throw std::invalid_argument{"Malformed MOUNT_LISTEN_STREAM path"};

	auto [local_path, lease] = Get(key);

	auto *m = alloc.New<Mount>(local_path + 1, alloc.DupZ(path), true, false);
	m->type = Mount::Type::BIND_FILE;

	auto i = mount_ns.mounts.before_begin();
	while (std::next(i) != mount_ns.mounts.end())
		++i;

	mount_ns.mounts.insert_after(i, *m);

	return std::move(lease);
}
