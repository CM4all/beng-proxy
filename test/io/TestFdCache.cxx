// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "event/FineTimerEvent.hxx"
#include "event/Loop.hxx"
#include "io/FdCache.hxx"
#include "io/Open.hxx"
#include "io/Temp.hxx"
#include "io/RecursiveDelete.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "system/Error.hxx"
#include "util/Cancellable.hxx"
#include "util/ScopeExit.hxx"

#ifdef HAVE_URING
#include "io/uring/Queue.hxx"
#endif

#include <gtest/gtest.h>

#include <errno.h>
#include <fcntl.h>
#include <linux/openat2.h> // for RESOLVE_*
#include <sys/stat.h> // for mkdirat()

using std::string_view_literals::operator""sv;

static constexpr struct open_how open_directory_path{
	.flags = O_PATH|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC,
	.resolve = RESOLVE_NO_MAGICLINKS,
};

class Request {
	FdCache &fd_cache;

	CancellablePointer cancel_ptr;

	SharedLease lease, other_lease;

	FileDescriptor fd;

	int error = -1;

	const bool discard;

public:
	bool flush_on_completion = false;

	Request(FdCache &_fd_cache, bool _discard) noexcept
		:fd_cache(_fd_cache), discard(_discard) {}

	~Request() noexcept {
		if (IsPending())
			Cancel();
	}

	auto &GetEventLoop() const noexcept {
		return fd_cache.GetEventLoop();
	}

	bool IsPending() const noexcept {
		return cancel_ptr;
	}

	int GetError() const noexcept {
		assert(!IsPending());
		assert(error >= 0);

		return error;
	}

	FileDescriptor GetFileDescriptor() const noexcept {
		assert(error == 0);
		assert(lease);

		return fd;
	}

	void DiscardLease() noexcept {
		lease = {};
	}

	SharedLease TakeLease() noexcept {
		return std::move(lease);
	}

	void ScheduleDiscardOtherLease(SharedLease &&_lease) noexcept {
		other_lease = std::move(_lease);
	}

	void Cancel() noexcept {
		assert(IsPending());

		other_lease = {};
		cancel_ptr.Cancel();
	}

	void Start(FileDescriptor directory,
		   std::string_view path,
		   const struct open_how &how) noexcept {
		assert(!IsPending());
		error = -1;
		DiscardLease();

		fd_cache.Get(directory, "/tmp/"sv, path, how, 0,
			     BIND_THIS_METHOD(OnSuccess), BIND_THIS_METHOD(OnError),
			     cancel_ptr);
	}

	void Wait() noexcept {
		if (!IsPending())
			return;

		GetEventLoop().Run();

		assert(!IsPending());
	}

private:
	void OnSuccess(FileDescriptor _fd, const struct statx &, SharedLease &&_lease) noexcept {
		assert(!lease);

		cancel_ptr = {};

		error = 0;

		if (!discard) {
			fd = _fd;
			lease = std::move(_lease);
		}

		other_lease = {};

		if (flush_on_completion)
			fd_cache.Flush();

		GetEventLoop().Break();
	}

	void OnError(int _error) noexcept {
		assert(!lease);

		cancel_ptr = {};

		error = _error;

		other_lease = {};

		if (flush_on_completion)
			fd_cache.Flush();

		GetEventLoop().Break();
	}
};

struct EventLoopUringInstance {
	EventLoop event_loop;

	FineTimerEvent break_timer{event_loop, BIND_THIS_METHOD(Break)};

	EventLoopUringInstance() {
#ifdef HAVE_URING
		event_loop.EnableUring(1024, 0);
#endif
	}

	/**
	 * Submit all pending io_uring operations.  This is sometimes
	 * necessary to make sure all "close" operations have finished
	 * before doing tests on file descriptors.
	 */
	void FlushUring() {
#ifdef HAVE_URING
		if (auto *queue = event_loop.GetUring()) {
			struct __kernel_timespec timeout{};
			queue->SubmitAndWaitDispatchCompletions(&timeout);
		}
#endif
	}

	void Break() noexcept {
		event_loop.Break();
	}

	void RunFor(Event::Duration duration) noexcept {
		break_timer.Schedule(duration);
		event_loop.Run();
	}
};

struct TestFdCacheInstance : EventLoopUringInstance {
	const UniqueFileDescriptor tmp = OpenTmpDir();
	const StringBuffer<16> tmp_name = MakeTempDirectory(tmp, 0700);
	const UniqueFileDescriptor dir = OpenPath(tmp, tmp_name, O_DIRECTORY);

	FdCache fd_cache{
		event_loop,
#ifdef HAVE_URING
		event_loop.GetUring(),
#endif
	};

	~TestFdCacheInstance() noexcept {
		fd_cache.BeginShutdown();
	}
};

/**
 * Start a lookup and cancel it before it finishes.  (Cancellation
 * only works with io_uring, because without io_uring, all operations
 * are synchronous.)
 */
TEST(TestFdCache, Cancel)
{
	TestFdCacheInstance instance;

	Request r{instance.fd_cache, true};

	r.Start(instance.dir, "/tmp/doesnt_exist"sv, open_directory_path);
	// the Request destructor cancels
}

/**
 * Open a directory that does not exist.  The lease is discarded from
 * within the handler.
 */
TEST(TestFdCache, DoesntExist)
{
	TestFdCacheInstance instance;

	Request r{instance.fd_cache, true}, r2{instance.fd_cache, true};

	r.Start(instance.dir, "/tmp/doesnt_exist"sv, open_directory_path);
	r.Wait();
	EXPECT_EQ(r.GetError(), ENOENT);

	/* the second request should finish instantly, error served
	   from the cache */
	r2.Start(instance.dir, "/tmp/doesnt_exist"sv, open_directory_path);
	EXPECT_FALSE(r2.IsPending());
	EXPECT_EQ(r2.GetError(), ENOENT);
}

TEST(TestFdCache, FlushDontReuse)
{
	TestFdCacheInstance instance;

	if (mkdirat(instance.dir.Get(), "dir", 0700) < 0)
		throw MakeErrno("mkdirat() failed");

	/* open the directory, keep holding a lease */
	Request r1{instance.fd_cache, false};
	r1.Start(instance.dir, "/tmp/dir"sv, open_directory_path);
	r1.Wait();
	EXPECT_EQ(r1.GetError(), 0);
	EXPECT_TRUE(r1.GetFileDescriptor().IsDefined());
	EXPECT_TRUE(r1.GetFileDescriptor().IsValid());

	/* open the directory again to see if it's the same FD */
	Request r2{instance.fd_cache, false};
	r2.Start(instance.dir, "/tmp/dir"sv, open_directory_path);
	EXPECT_FALSE(r2.IsPending());
	EXPECT_EQ(r2.GetError(), 0);
	EXPECT_TRUE(r2.GetFileDescriptor().IsDefined());
	EXPECT_TRUE(r2.GetFileDescriptor().IsValid());
	EXPECT_EQ(r2.GetFileDescriptor(), r1.GetFileDescriptor());

	/* flush the cache; the leases remain valid */
	instance.fd_cache.Flush();
	EXPECT_TRUE(r1.GetFileDescriptor().IsValid());
	EXPECT_TRUE(r2.GetFileDescriptor().IsValid());

	/* open the directory yet again; after the flush, it must be a different FD */
	Request r3{instance.fd_cache, false};
	r3.Start(instance.dir, "/tmp/dir"sv, open_directory_path);
	r3.Wait();
	EXPECT_EQ(r3.GetError(), 0);
	EXPECT_TRUE(r3.GetFileDescriptor().IsDefined());
	EXPECT_TRUE(r3.GetFileDescriptor().IsValid());
	EXPECT_NE(r3.GetFileDescriptor(), r1.GetFileDescriptor());

	/* initiate shutdown; abandoned FDs will be closed
	   instantly */
	instance.fd_cache.BeginShutdown();

	/* discard the flushed leases: FD must become invalid */
	EXPECT_TRUE(r1.GetFileDescriptor().IsValid());
	EXPECT_TRUE(r2.GetFileDescriptor().IsValid());

	r1.DiscardLease();
	instance.FlushUring();
	EXPECT_TRUE(r2.GetFileDescriptor().IsValid());

	const auto fd = r2.GetFileDescriptor();
	r2.DiscardLease();
	instance.FlushUring();
	EXPECT_FALSE(fd.IsValid());
}

/**
 * Call FdCache::Flush() from within the completionn handler.  This
 * attempts to trigger an old UAF bug.
 */
TEST(TestFdCache, FlushOnCompletion)
{
	TestFdCacheInstance instance;

	Request r{instance.fd_cache, true};

	if (mkdirat(instance.dir.Get(), "dir", 0700) < 0)
		throw MakeErrno("mkdirat() failed");

	r.flush_on_completion = true;

	r.Start(instance.dir, "/tmp/dir"sv, open_directory_path);
	r.Wait();
	EXPECT_EQ(r.GetError(), 0);
}

TEST(TestFdCache, Inotify)
{
	TestFdCacheInstance instance;

	if (mkdirat(instance.dir.Get(), "dir", 0700) < 0)
		throw MakeErrno("mkdirat() failed");

	/* open the directory, keep holding a lease */
	Request r1{instance.fd_cache, false};
	r1.Start(instance.dir, "/tmp/dir"sv, open_directory_path);
	r1.Wait();
	EXPECT_EQ(r1.GetError(), 0);
	EXPECT_TRUE(r1.GetFileDescriptor().IsDefined());
	EXPECT_TRUE(r1.GetFileDescriptor().IsValid());

	/* rename the directory, triggering an inotify event */
	if (renameat(instance.dir.Get(), "dir",
		     instance.dir.Get(), "renamed") < 0)
		throw MakeErrno("renameat() failed");

	if (mkdirat(instance.dir.Get(), "dir", 0700) < 0)
		throw MakeErrno("mkdirat() failed");

	instance.RunFor(std::chrono::milliseconds{1});

	/* open the directory again; after the inotify, it must be a
	   different FD */
	Request r2{instance.fd_cache, false};
	r2.Start(instance.dir, "/tmp/dir"sv, open_directory_path);
	r2.Wait();
	EXPECT_EQ(r2.GetError(), 0);
	EXPECT_TRUE(r2.GetFileDescriptor().IsDefined());
	EXPECT_TRUE(r2.GetFileDescriptor().IsValid());
	EXPECT_NE(r2.GetFileDescriptor(), r1.GetFileDescriptor());

	/* discard the expired lease: FD must become invalid */
	EXPECT_TRUE(r1.GetFileDescriptor().IsValid());
	EXPECT_TRUE(r2.GetFileDescriptor().IsValid());

	const auto fd1 = r1.GetFileDescriptor();
	r1.DiscardLease();

	instance.FlushUring();

	EXPECT_FALSE(fd1.IsValid());
	EXPECT_TRUE(r2.GetFileDescriptor().IsValid());
}
