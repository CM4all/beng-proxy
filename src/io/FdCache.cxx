// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "FdCache.hxx"
#include "event/Loop.hxx"
#include "system/Error.hxx"
#include "io/FileAt.hxx"
#include "io/Open.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "io/linux/ProcPath.hxx"
#include "util/Cancellable.hxx"
#include "util/djb_hash.hxx"
#include "util/SpanCast.hxx"

#ifdef HAVE_URING
#include "io/uring/Handler.hxx"
#include "io/uring/Open.hxx"
#include "io/uring/Close.hxx"
#include "io/uring/Queue.hxx"
#endif

#include <cassert>
#include <cerrno>
#include <string>

#include <fcntl.h> // for AT_EMPTY_PATH
#include <linux/openat2.h> // for struct open_how
#include <sys/inotify.h>
#include <sys/stat.h> // for struct statx

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
	: IntrusiveHashSetHook<>,
	  IntrusiveListHook<>,
	  InotifyWatch,
#ifdef HAVE_URING
	  Uring::OpenHandler,
	  Uring::Operation,
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

	struct statx stx;

	unsigned next_stx_mask = 0;

	UniqueFileDescriptor fd;

	int error = 0;

	std::chrono::steady_clock::time_point expires;

	Item(FdCache &_cache,
	     std::string_view _path, uint_least64_t _flags,
	     std::chrono::steady_clock::time_point _expires) noexcept
		:InotifyWatch(_cache.inotify_manager), cache(_cache),
		 path(_path), flags(_flags),
		 expires(_expires)
	{
		stx.stx_mask = 0;
	}

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
		RemoveWatch();
		expires = std::chrono::steady_clock::time_point{};
	}

	bool IsDisabled() const noexcept {
		return expires <= std::chrono::steady_clock::time_point{};
	}

	bool IsUnused() const noexcept {
		return SharedAnchor::IsAbandoned() && requests.empty();
	}

	void Start(FileDescriptor directory, std::size_t strip_length,
		   const struct open_how &how,
		   unsigned requested_stx_mask) noexcept;

	void Get(SuccessCallback on_success,
		 ErrorCallback on_error,
		 unsigned requested_stx_mask,
		 CancellablePointer &cancel_ptr) noexcept;

private:
	void StartStatx() noexcept;

	/**
	 * Invoke the on_success callbacks of all requests.
	 *
	 * After returning, this object may be deleted.
	 */
	void InvokeSuccess() noexcept {
		assert(fd.IsDefined());
		assert(!error);

		/* make sure the item doesn't get abandoned while
		   submitting the result to all handlers */
		const SharedLease lock{*this};

		requests.clear_and_dispose([this](Request *r) {
			auto on_success = r->on_success;
			delete r;
			on_success(fd, stx, *this);
		});
	}

	/**
	 * Invoke the on_success callbacks of all requests.
	 *
	 * After returning, this object may be deleted.
	 */
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

	void RegisterInotify() noexcept {
		if (next_stx_mask != 0)
			/* this kludge-y check omits inotify
			   registrations for regular files */
			return;

		/* tell the kernel to notify us when the directory
		   gets deleted or moved; if that happens, we need to
		   discard this item */
		TryAddWatch(ProcFdPath(fd),
			    IN_MOVE_SELF|IN_ONESHOT|IN_ONLYDIR|IN_MASK_CREATE);
	}

	void SetError(int _error) noexcept {
		/* short expiry for negative items (because we have no
		   inotify here) */
		// TODO watch the parent directory
		if (_error == ENOENT)
			cache.SetExpiresSoon(*this, std::chrono::seconds{1});

		/* if this error happened during statx(), then we have
		   a file descriptor already; discard it because we
		   don't want to have a file descriptor that cannot
		   even statx() - it's probably a stale */
		if (fd.IsDefined()) [[unlikely]] {
#ifdef HAVE_URING
			Uring::Close(cache.uring_queue, fd.Release());
#else
			fd.Close();
#endif
		}

		error = _error;
		InvokeError();
	}

	// virtual methods from InotifyWatch
	void OnInotify(unsigned mask, const char *name) noexcept override;

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
		RegisterInotify();

		if (next_stx_mask != 0) {
			StartStatx();
			return;
		}

		InvokeSuccess();
	}

	void OnOpenError(int _error) noexcept override {
		assert(_error != 0);
		assert(!fd.IsDefined());
		assert(error == 0);
		assert(uring_open != nullptr);

		delete uring_open;
		uring_open = nullptr;

		SetError(_error);
	}

	/* virtual methods from Uring::Operation */
	void OnUringCompletion(int res) noexcept override {
		if (res == 0)
			InvokeSuccess();
		else
			SetError(-res);
	}
#endif // HAVE_URING

	/* virtual methods from SharedAnchor */
	void OnAbandoned() noexcept override {
		if (IsDisabled()) {
			delete this;
		}
	}

	void OnBroken() noexcept override {
		assert(!IsAbandoned());

		if (!IsDisabled()) {
			IntrusiveListHook::unlink();
			IntrusiveHashSetHook::unlink();
			Disable();
		}
	}
};

inline void
FdCache::Item::Start(FileDescriptor directory, std::size_t strip_length,
		     const struct open_how &how,
		     unsigned requested_stx_mask) noexcept
{
	assert(!fd.IsDefined());
	assert(!error);
	assert(strip_length <= path.length());

	/* this requested_stx_mask parameter is only passed here to
	   prevent the inotify_add_watch() call for regular files */
	next_stx_mask |= requested_stx_mask;

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
		fd = TryOpen({directory, p}, how);
		if (fd.IsDefined()) {
			RegisterInotify();
			InvokeSuccess();
		} else {
			SetError(errno);
		}
#ifdef HAVE_URING
	}
#endif
}

inline void
FdCache::Item::StartStatx() noexcept
{
	assert(fd.IsDefined());

#ifdef HAVE_URING
	if (IsUringPending())
		return;

	if (cache.uring_queue != nullptr) {
		auto &queue = *cache.uring_queue;
		auto &s = queue.RequireSubmitEntry();
		io_uring_prep_statx(&s, fd.Get(), "", AT_EMPTY_PATH, next_stx_mask, &stx);
		queue.Push(s, *this);
	} else {
#endif // HAVE_URING
		if (statx(fd.Get(), "", AT_EMPTY_PATH, next_stx_mask, &stx) == 0)
			InvokeSuccess();
		else
			SetError(errno);
#ifdef HAVE_URING
	}
#endif
}

inline void
FdCache::Item::Get(SuccessCallback on_success,
		   ErrorCallback on_error,
		   unsigned requested_stx_mask,
		   CancellablePointer &cancel_ptr) noexcept
{
	if (fd.IsDefined() && (requested_stx_mask & ~stx.stx_mask) == 0) {
		on_success(fd, stx, *this);
	} else if (error) {
		on_error(error);
	} else {
		auto *request = new Request(on_success, on_error, cancel_ptr);
		requests.push_back(*request);

		next_stx_mask |= requested_stx_mask;

		if (fd.IsDefined())
			StartStatx();
	}
}

inline FdCache::Key
FdCache::ItemGetKey::operator()(const Item &item) const noexcept
{
	return {item.path, item.flags};
}

FdCache::FdCache(EventLoop &event_loop
#ifdef HAVE_URING
		, Uring::Queue *_uring_queue
#endif // HAVE_URING
	) noexcept
	:expire_timer(event_loop, BIND_THIS_METHOD(Expire)),
#ifdef HAVE_URING
	 uring_queue(_uring_queue),
#endif
	 inotify_manager(event_loop)
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
	map.clear();

	chronological_list.clear_and_dispose([](auto *item){
		item->Disable();
		if (item->IsUnused())
			delete item;
	});
}

void
FdCache::BeginShutdown() noexcept
{
	expire_timer.Cancel();
	inotify_manager.BeginShutdown();

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

		if (strip_path.ends_with('/'))
			return strip_path.size();
	}

	return 0;
}

void
FdCache::Get(FileDescriptor directory,
	     std::string_view strip_path,
	     std::string_view path,
	     const struct open_how &how,
	     unsigned stx_mask,
	     SuccessCallback on_success,
	     ErrorCallback on_error,
	     CancellablePointer &cancel_ptr) noexcept
{
	assert(!path.empty());
	assert(path.starts_with('/'));
	assert(!path.ends_with('/'));

	const auto now = GetEventLoop().SteadyNow();

	auto [it, inserted] = map.insert_check(Key{path, how.flags});
	if (!inserted) {
		assert(!IsShuttingDown());
		assert(expire_timer.IsPending());

		auto &item = *it;
		assert(!item.IsDisabled());

		if (now < item.expires) [[likely]] {
			/* use this item */
			item.Get(on_success, on_error, stx_mask, cancel_ptr);
			assert(expire_timer.IsPending());
			return;
		}

		/* item expired: remove it and create a new one */
		item.Disable();
		it = map.erase(it);
		chronological_list.erase(chronological_list.iterator_to(item));
		if (item.IsUnused())
			delete &item;
	}

	if (empty() && !IsShuttingDown())
		/* the cache is about to become non-empty and from now
		   on, we need to expire all items periodically */
		expire_timer.Schedule(std::chrono::seconds{10});

	std::chrono::steady_clock::duration expires = std::chrono::minutes{1};
	if (stx_mask != 0)
		/* regular files (stx_mask!=0) expire faster; we don't
		   have inotify for them */
		// TODO revalidate expired items instead of discarding them
		expires = std::chrono::seconds{10};

	auto *item = new Item(*this, path, how.flags,
			      GetEventLoop().SteadyNow() + expires);

	if (!IsShuttingDown()) {
		[[likely]]
		chronological_list.push_back(*item);
		map.insert_commit(it, *item);
	} else {
		/* create a "disabled" item that is not in the map; it
		   will be deleted as soon as the caller releases the
		   ShardLease */
		item->Disable();
	}

	/* hold a lease until Get() finishes so the item doesn't get
	   destroyed if Start() finishes synchronously */
	const SharedLease lock{*item};

	item->Start(directory, StripLength(strip_path, path), how, stx_mask);
	item->Get(on_success, on_error, stx_mask, cancel_ptr);

	assert(IsShuttingDown() != expire_timer.IsPending());
}

inline void
FdCache::SetExpiresSoon(Item &item, Event::Duration expiry) noexcept
{
	assert(!chronological_list.empty());
	assert(IsShuttingDown() != expire_timer.IsPending());

	const auto new_expires = std::min(GetEventLoop().SteadyNow() + expiry,
					  chronological_list.front().expires);
	/* the new expires must not be later than
	   chronological_list.front() or else the chronological_list
	   isn't sorted anymore; really sorting that list would jut
	   add overhead, and I guess just using std::min() is the best
	   compromise */
	if (new_expires >= item.expires)
		/* not sooner than the old time */
		return;

	item.expires = new_expires;

	/* move to the front, because it's now the earliest expires */
	chronological_list.erase(chronological_list.iterator_to(item));
	chronological_list.push_front(item);

	/* re-schedule the timer to make sure this item really gets
	   flushed soon */
	expire_timer.ScheduleEarlier(expiry);
}

void
FdCache::Expire() noexcept
{
	const auto now = GetEventLoop().SteadyNow();

	for (auto i = chronological_list.begin(); i != chronological_list.end();) {
		if (now < i->expires)
			break;

		auto &item = *i;
		map.erase(map.iterator_to(item));
		i = chronological_list.erase(i);

		item.Disable();
		if (item.IsUnused())
			delete &item;
	}

	if (!empty() && !IsShuttingDown())
		expire_timer.Schedule(std::chrono::seconds{10});
}

void
FdCache::Item::OnInotify([[maybe_unused]] unsigned mask,
			 [[maybe_unused]] const char *name) noexcept
{
	assert(!IsWatching()); // it's oneshot

	IntrusiveListHook::unlink();
	IntrusiveHashSetHook::unlink();
	Disable();

	if (IsUnused())
		/* unused, delete immediately */
		delete this;

}
