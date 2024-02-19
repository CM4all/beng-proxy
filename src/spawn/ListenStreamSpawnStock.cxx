// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "ListenStreamSpawnStock.hxx"
#include "spawn/ExitListener.hxx"
#include "spawn/Interface.hxx"
#include "spawn/Mount.hxx"
#include "spawn/Prepared.hxx"
#include "spawn/ProcessHandle.hxx"
#include "translation/Handler.hxx"
#include "translation/Request.hxx"
#include "translation/Response.hxx"
#include "translation/Service.hxx"
#include "pool/Ptr.hxx"
#include "pool/pool.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "event/CoarseTimerEvent.hxx"
#include "event/SocketEvent.hxx"
#include "net/TempListener.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "util/Cancellable.hxx"
#include "util/DeleteDisposer.hxx"
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

class ListenStreamSpawnStock::Item final
	: public IntrusiveHashSetHook<IntrusiveHookMode::AUTO_UNLINK>,
	  public SharedAnchor,
	  private TranslateHandler,
	  private ExitListener
{
	TranslationService &translation_service;
	SpawnService &spawn_service;

	const std::string key;

	std::string tags;

	TempListener temp;

	SocketEvent socket;

	CoarseTimerEvent idle_timer;

	PoolPtr translation_pool;

	CancellablePointer translation_cancel_ptr;

	std::exception_ptr error;

	std::unique_ptr<ChildProcessHandle> process;

	bool fade = false;

public:
	Item(EventLoop &event_loop,
	     TranslationService &_translation_service,
	     SpawnService &_spawn_service,
	     std::string_view _key)
		:translation_service(_translation_service),
		 spawn_service(_spawn_service),
		 key(_key),
		 socket(event_loop, BIND_THIS_METHOD(OnSocketReady)),
		 idle_timer(event_loop, BIND_THIS_METHOD(OnIdleTimeout)) {

		auto s = temp.Create(SOCK_STREAM, 16);
		socket.Open(s.Release());
		socket.ScheduleRead();

		// TODO protect the socket against access from the host
		chmod(temp.GetPath(), 0666);
	}

	~Item() noexcept {
		if (translation_cancel_ptr)
			translation_cancel_ptr.Cancel();

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

		if (IsAbandoned())
			idle_timer.Schedule({});
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
	void OnSocketReady(unsigned) noexcept {
		assert(!translation_pool);
		assert(!translation_cancel_ptr);

		socket.Cancel();

		translation_pool = pool_new_libc(nullptr, "ListenStreamSpawnStock::Item::Translation");

		const TranslateRequest request{
			.mount_listen_stream = AsBytes(key),
		};

		translation_service.SendRequest(AllocatorPtr{translation_pool}, request,
						{}, *this, translation_cancel_ptr);
	}

	void OnIdleTimeout() noexcept {
		delete this;
	}

	// virtual methods from class SharedAnchor
	void OnAbandoned() noexcept override {
		if (fade || translation_cancel_ptr) {
			/* destroy immediately if we're in "fade" mode
			   or if we're currently waiting for the
			   translation server (which means the client
			   has given up very quickly, and this process
			   will probably never be used again) */
			delete this;
			return;
		}

		/* keep the process around for some time */
		idle_timer.Schedule(std::chrono::minutes{5});
	}

	// virtual methods from class TranslateHandler
	void OnTranslateResponse(UniquePoolPtr<TranslateResponse> response) noexcept override;
	void OnTranslateError(std::exception_ptr _error) noexcept override;

	// virtual methods from class ExitListener
	void OnChildProcessExit(int status) noexcept override;
};

static std::unique_ptr<ChildProcessHandle>
DoSpawn(SpawnService &service, const char *name,
	UniqueSocketDescriptor socket,
	const TranslateResponse &response)
{
	if (response.status != HttpStatus{}) {
		if (response.message != nullptr)
			throw FmtRuntimeError("Status {} from translation server: {}",
					      static_cast<unsigned>(response.status),
					      response.message);

		throw FmtRuntimeError("Status {} from translation server",
				      static_cast<unsigned>(response.status));
	}

	if (response.execute == nullptr)
		throw std::runtime_error("No EXECUTE from translation server");

	PreparedChildProcess p;
	p.SetStdin(std::move(socket));
	p.args.push_back(response.execute);

	for (const char *arg : response.args) {
		if (p.args.size() >= 4096)
			throw std::runtime_error("Too many APPEND packets from translation server");

		p.args.push_back(arg);
	}

	response.child_options.CopyTo(p);

	return service.SpawnChildProcess(name, std::move(p));
}


void
ListenStreamSpawnStock::Item::OnTranslateResponse(UniquePoolPtr<TranslateResponse> response) noexcept
try {
	assert(translation_cancel_ptr);
	assert(translation_pool);

	translation_cancel_ptr = {};

	tags = response->child_options.tag;

	process = DoSpawn(spawn_service, temp.GetPath(),
			  UniqueSocketDescriptor{socket.ReleaseSocket()},
			  *response);

	process->SetExitListener(*this);
} catch (...) {
	// TODO log
	error = std::current_exception();
	Fade();
}

void
ListenStreamSpawnStock::Item::OnTranslateError(std::exception_ptr _error) noexcept
{
	assert(translation_cancel_ptr);
	assert(translation_pool);

	translation_cancel_ptr = {};

	// TODO log
	error = std::move(_error);
	Fade();
}

void
ListenStreamSpawnStock::Item::OnChildProcessExit([[maybe_unused]] int status) noexcept
{
	Fade();
}

inline std::size_t
ListenStreamSpawnStock::ItemHash::operator()(std::string_view key) const noexcept
{
	return djb_hash(AsBytes(key));
}

inline std::string_view
ListenStreamSpawnStock::ItemGetKey::operator()(const Item &item) const noexcept
{
	return item.GetKey();
}

ListenStreamSpawnStock::ListenStreamSpawnStock(EventLoop &_event_loop,
					       TranslationService &_translation_service,
					       SpawnService &_spawn_service) noexcept
	:event_loop(_event_loop),
	 translation_service(_translation_service),
	 spawn_service(_spawn_service) {}

ListenStreamSpawnStock::~ListenStreamSpawnStock() noexcept
{
	items.clear_and_dispose(DeleteDisposer{});
}

void
ListenStreamSpawnStock::FadeAll() noexcept
{
	items.for_each([](auto &i){
		i.Fade();
	});
}

void
ListenStreamSpawnStock::FadeTag(std::string_view tag) noexcept
{
	items.for_each([tag](auto &i){
		if (i.IsTag(tag))
			i.Fade();
	});
}

std::pair<const char *, SharedLease>
ListenStreamSpawnStock::Get(std::string_view key)
{
	auto [it, inserted] = items.insert_check_if(key, [](const auto &i){ return i.CanUse(); });
	if (inserted) {
		auto *item = new Item(event_loop, translation_service,
				      spawn_service, key);
		it = items.insert_commit(it, *item);
	} else {
		it->Borrow();
	}

	return {it->GetPath(), *it};
}

SharedLease
ListenStreamSpawnStock::Apply(AllocatorPtr alloc, MountNamespaceOptions &mount_ns)
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
