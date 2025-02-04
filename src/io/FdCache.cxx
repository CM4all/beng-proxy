// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "FdCache.hxx"
#include "event/Loop.hxx"
#include "system/Error.hxx"
#include "io/FileAt.hxx"
#include "io/Open.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "io/linux/ProcPath.hxx"
#include "util/Cancellable.hxx"
#include "util/DeleteDisposer.hxx"
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
	: IntrusiveHashSetHook<IntrusiveHookMode::AUTO_UNLINK, KeyTag>,
	  IntrusiveHashSetHook<IntrusiveHookMode::AUTO_UNLINK, InotifyTag>,
	  IntrusiveListHook<IntrusiveHookMode::AUTO_UNLINK>,
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

	int watch_descriptor = -1;

	std::chrono::steady_clock::time_point expires;

	Item(FdCache &_cache,
	     std::string_view _path, uint_least64_t _flags,
	     std::chrono::steady_clock::time_point _expires) noexcept
		:cache(_cache),
		 path(_path), flags(_flags),
		 expires(_expires)
	{
		stx.stx_mask = 0;
	}

	~Item() noexcept {
		assert(requests.empty());
		assert(IsAbandoned());

		if (watch_descriptor >= 0) {
			cache.inotify_event.RemoveWatch(watch_descriptor);
			cache.inotify_map.erase(cache.inotify_map.iterator_to(*this));
		}

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
		 unsigned requested_stx_mask,
		 CancellablePointer &cancel_ptr) noexcept;

private:
	void StartStatx() noexcept;

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
		watch_descriptor = cache.inotify_event
			.TryAddWatch(ProcFdPath(fd),
				     IN_DELETE_SELF|IN_MOVE_SELF|IN_ONESHOT|IN_ONLYDIR|IN_MASK_CREATE);

		if (watch_descriptor >= 0)
			cache.inotify_map.insert(*this);
	}

	void SetError(int _error) noexcept {
		/* short expiry for negative items (because we have no
		   inotify here) */
		// TODO watch the parent directory
		if (_error == ENOENT)
			cache.SetExpiresSoon(*this, std::chrono::seconds{1});

		error = _error;
		InvokeError();
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
		RegisterInotify();

		if (next_stx_mask != 0) {
			StartStatx();
			return;
		}

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

		SetError(_error);

		assert(requests.empty());

		if (IsDisabled() && IsAbandoned())
			delete this;
	}

	/* virtual methods from Uring::Operation */
	void OnUringCompletion(int res) noexcept override {
		if (res == 0)
			InvokeSuccess();
		else
			SetError(-res);

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

inline int
FdCache::ItemGetInotify::operator()(const Item &item) const noexcept
{
	return item.watch_descriptor;
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
	 inotify_event(event_loop, *this)
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
	inotify_event.Disable();

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

	auto [it, inserted] = map.insert_check(Key{path, how.flags});
	if (inserted) {
		if (empty() && enabled)
			/* the cache is about to become non-empty and
			   from now on, we need to expire all items
			   periodically */
			expire_timer.Schedule(std::chrono::seconds{10});

		std::chrono::steady_clock::duration expires = std::chrono::minutes{1};
		if (stx_mask != 0)
			/* regular files (stx_mask!=0) expire faster;
			   we don't have inotify for them */
			// TODO revalidate expired items instead of discarding them
			expires = std::chrono::seconds{10};

		auto *item = new Item(*this, path, how.flags,
				      GetEventLoop().SteadyNow() + expires);
		chronological_list.push_back(*item);
		map.insert_commit(it, *item);

		if (!enabled)
			item->Disable();

		/* hold a lease until Get() finishes so the item
		   doesn't get destroyed if Start() finishes
		   synchronously */
		const SharedLease lock{*item};

		item->Start(directory, StripLength(strip_path, path), how);
		item->Get(on_success, on_error, stx_mask, cancel_ptr);
	} else
		it->Get(on_success, on_error, stx_mask, cancel_ptr);
}

inline void
FdCache::SetExpiresSoon(Item &item, Event::Duration expiry) noexcept
{
	assert(!chronological_list.empty());
	assert(enabled == expire_timer.IsPending());

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
	expire_timer.Schedule(expiry);
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

void
FdCache::OnInotify(int wd, [[maybe_unused]] unsigned mask,
		   [[maybe_unused]] const char *name)
{
	if (auto i = inotify_map.find(wd); i != inotify_map.end()) {
		assert(i->watch_descriptor == wd);
		inotify_map.erase(i);
		i->watch_descriptor = -1;

		if (i->IsUnused())
			/* unused, delete immediately */
			delete &*i;
		else
			/* still in use, delete later */
			i->Disable();
	}
}

void
FdCache::OnInotifyError([[maybe_unused]] std::exception_ptr error) noexcept
{
	// TODO log?
}
