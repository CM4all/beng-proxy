// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "FdCache.hxx"
#include "event/Loop.hxx"
#include "system/Error.hxx"
#include "system/openat2.h"
#include "io/FileAt.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "util/Cancellable.hxx"
#include "util/DeleteDisposer.hxx"
#include "util/djb_hash.hxx"
#include "util/SpanCast.hxx"

#ifdef HAVE_URING
#include "io/uring/Handler.hxx"
#include "io/uring/Open.hxx"
#include "io/uring/Close.hxx"
#endif

#include <cassert>
#include <cerrno>
#include <string>

using std::string_view_literals::operator""sv;

inline std::size_t
FdCache::Key::Hash::operator()(const Key &key) const noexcept
{
	return djb_hash(AsBytes(key.path)) ^ key.flags;
}

/**
 * One item in the cache.  It has one of the following states:
 *
 * - initial: the object has just been constructed
 *
 * - started: Start() has been called; if io_uring is used, then
 *   #uring_open is now set and we are waiting for the result from
 *   io_uring
 *
 * - succeded: #fd is set and all callbacks have been invoked
 *
 * - failed: #error is set and all callbacks have been invoked
 *
 * Before the operation finishes ("succeded" or "failed"), the
 * #requests list contains a list of callbacks that are interested in
 * the result.
 */
struct FdCache::Item final
	: IntrusiveHashSetHook<IntrusiveHookMode::AUTO_UNLINK>,
	  IntrusiveListHook<IntrusiveHookMode::AUTO_UNLINK>,
#ifdef HAVE_URING
	  Uring::OpenHandler,
#endif // HAVE_URING
	  SharedAnchor
{
	FdCache &cache;

	const std::string path;

	const uint_least64_t flags;

	struct Request final : IntrusiveListHook<IntrusiveHookMode::AUTO_UNLINK>, Cancellable {
		const SuccessCallback on_success;
		const ErrorCallback on_error;

		Request(SuccessCallback _on_success,
			ErrorCallback _on_error,
			CancellablePointer &cancel_ptr) noexcept
			:on_success(_on_success), on_error(_on_error)
		{
			cancel_ptr = *this;
		}

		void Cancel() noexcept override {
			delete this;
		}
	};

	IntrusiveList<Request> requests;

#ifdef HAVE_URING
	Uring::Open *uring_open = nullptr;
#endif // HAVE_URING

	UniqueFileDescriptor fd;

	int error = 0;

	std::chrono::steady_clock::time_point expires;

	Item(FdCache &_cache,
	     std::string_view _path, uint_least64_t _flags,
	     std::chrono::steady_clock::time_point now) noexcept
		:cache(_cache),
		 path(_path), flags(_flags),
		 expires(now + std::chrono::minutes{1}) {}

	~Item() noexcept {
		assert(requests.empty());
		assert(IsAbandoned());

#ifdef HAVE_URING
		if (uring_open != nullptr) {
			assert(!fd.IsDefined());
			assert(!error);

			uring_open->Cancel();
		}

		if (fd.IsDefined())
			Uring::Close(cache.uring_queue, fd.Release());
#endif // HAVE_URING
	}

	void Disable() noexcept {
		expires = std::chrono::steady_clock::time_point{};
	}

	bool IsDisabled() const noexcept {
		return expires <= std::chrono::steady_clock::time_point{};
	}

	bool IsUnused() const noexcept {
		return SharedAnchor::IsAbandoned() && requests.empty();
	}

	void Start(FileDescriptor directory, std::size_t strip_length,
		   const struct open_how &how) noexcept;

	void Get(SuccessCallback on_success,
		 ErrorCallback on_error,
		 CancellablePointer &cancel_ptr) noexcept;

private:
	void InvokeSuccess() noexcept {
		assert(fd.IsDefined());
		assert(!error);

		/* make sure the item doesn't get abandoned while
		   submitting the result to all handlers */
		const SharedLease lock{*this};

		requests.clear_and_dispose([this](Request *r) {
			auto on_success = r->on_success;
			delete r;
			on_success(fd, *this);
		});
	}

	void InvokeError() noexcept {
		assert(!fd.IsDefined());
		assert(error);

		/* make sure the item doesn't get abandoned while
		   submitting the result to all handlers */
		const SharedLease lock{*this};

		requests.clear_and_dispose([this](Request *r) {
			auto on_error = r->on_error;
			delete r;
			on_error(error);
		});
	}

#ifdef HAVE_URING
	/* virtual methods from Uring::OpenHandler */
	void OnOpen(UniqueFileDescriptor _fd) noexcept override {
		assert(_fd.IsDefined());
		assert(!fd.IsDefined());
		assert(!error);
		assert(uring_open != nullptr);

		delete uring_open;
		uring_open = nullptr;

		fd = std::move(_fd);
		InvokeSuccess();

		assert(requests.empty());

		if (IsDisabled() && IsAbandoned())
			delete this;
	}

	void OnOpenError(int _error) noexcept override {
		assert(_error != 0);
		assert(!fd.IsDefined());
		assert(error == 0);
		assert(uring_open != nullptr);

		delete uring_open;
		uring_open = nullptr;

		error = _error;
		InvokeError();

		assert(requests.empty());

		if (IsDisabled() && IsAbandoned())
			delete this;
	}
#endif // HAVE_URING

	/* virtual methods from SharedAnchor */
	void OnAbandoned() noexcept override {
		if (IsDisabled())
			delete this;
	}
};

inline void
FdCache::Item::Start(FileDescriptor directory, std::size_t strip_length,
		     const struct open_how &how) noexcept
{
	assert(!fd.IsDefined());
	assert(!error);
	assert(strip_length <= path.length());

	const char *p = path.c_str() + strip_length;
	if (*p == 0)
		p = ".";

#ifdef HAVE_URING
	assert(uring_open == nullptr);

	if (cache.uring_queue != nullptr) {
		uring_open = new Uring::Open(*cache.uring_queue, *this);
		uring_open->StartOpen({directory, p}, how);
	} else {
#endif // HAVE_URING
		int _fd = openat2(directory.Get(), p,
				  &how, sizeof(how));
		if (_fd >= 0) {
			fd = UniqueFileDescriptor{_fd};
			InvokeSuccess();
		} else {
			error = errno;
			InvokeError();
		}
#ifdef HAVE_URING
	}
#endif
}

inline void
FdCache::Item::Get(SuccessCallback on_success,
		   ErrorCallback on_error,
		   CancellablePointer &cancel_ptr) noexcept
{
	if (fd.IsDefined()) {
		on_success(fd, *this);
	} else if (error) {
		on_error(error);
	} else {
		auto *request = new Request(on_success, on_error, cancel_ptr);
		requests.push_back(*request);
	}
}

inline FdCache::Key
FdCache::ItemGetKey::operator()(const Item &item) noexcept
{
	return {item.path, item.flags};
}

FdCache::FdCache(EventLoop &event_loop
#ifdef HAVE_URING
		, Uring::Queue *_uring_queue
#endif // HAVE_URING
	) noexcept
	:expire_timer(event_loop, BIND_THIS_METHOD(Expire))
#ifdef HAVE_URING
	, uring_queue(_uring_queue)
#endif
{
}

FdCache::~FdCache() noexcept
{
	assert(map.empty());
	assert(chronological_list.empty());
}

void
FdCache::Flush() noexcept
{
	for (auto &i : chronological_list)
		i.Disable();

	Expire();
}

void
FdCache::Disable() noexcept
{
	enabled = false;

	expire_timer.Cancel();

	Flush();
}

/**
 * Determine how many characters shall be stripped at the beginning of
 * #path to make it relative to #strip_path.  Returns 0 on mismatch.
 */
[[gnu::pure]]
static std::size_t
StripLength(std::string_view strip_path, std::string_view path)
{
	if (strip_path.empty())
		return 0;

	if (path.starts_with(strip_path)) {
		if (path.size() == strip_path.size())
			return path.size();

		if (path[strip_path.size()] == '/')
			return strip_path.size() + 1;
	}

	return 0;
}

void
FdCache::Get(FileDescriptor directory,
	     std::string_view strip_path,
	     std::string_view path,
	     const struct open_how &how,
	     SuccessCallback on_success,
	     ErrorCallback on_error,
	     CancellablePointer &cancel_ptr) noexcept
{
	assert(!path.empty());
	assert(path.starts_with('/'));
	assert(!path.ends_with('/'));

	auto [it, inserted] = map.insert_check(Key{path, how.flags});
	if (inserted) {
		if (empty() && enabled)
			/* the cache is about to become non-empty and
			   from now on, we need to expire all items
			   periodically */
			expire_timer.Schedule(std::chrono::seconds{10});

		auto *item = new Item(*this, path, how.flags,
				      GetEventLoop().SteadyNow());
		chronological_list.push_back(*item);
		map.insert_commit(it, *item);

		if (!enabled)
			item->Disable();

		/* hold a lease until Get() finishes so the item
		   doesn't get destroyed if Start() finishes
		   synchronously */
		const SharedLease lock{*item};

		item->Start(directory, StripLength(strip_path, path), how);
		item->Get(on_success, on_error, cancel_ptr);
	} else
		it->Get(on_success, on_error, cancel_ptr);
}

void
FdCache::Expire() noexcept
{
	const auto now = GetEventLoop().SteadyNow();

	for (auto i = chronological_list.begin(); i != chronological_list.end();) {
		if (now < i->expires)
			break;

		if (i->IsUnused())
			i = chronological_list.erase_and_dispose(i, DeleteDisposer{});
		else
			++i;
	}

	if (!empty() && enabled)
		expire_timer.Schedule(std::chrono::seconds{10});
}
