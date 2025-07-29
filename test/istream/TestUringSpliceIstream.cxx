// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "../TestInstance.hxx"
#include "../OpenFileLease.hxx"
#include "CountIstreamSink.hxx"
#include "istream/UringSpliceIstream.hxx"
#include "istream/UnusedPtr.hxx"
#include "pool/pool.hxx"
#include "event/DeferEvent.hxx"
#include "io/uring/Queue.hxx"
#include "system/Error.hxx"

#include <gtest/gtest.h>

namespace {

static std::pair<UnusedIstreamPtr, size_t>
MakeUringSpliceIstream(struct pool &pool, EventLoop &event_loop, Uring::Queue &uring, const char *path)
{
	auto [fd, lease, size] = OpenFileLease(pool, path);

	return {
		NewUringSpliceIstream(event_loop, uring, nullptr, pool,
				      path, fd, std::move(lease),
				      0, size),
		size,
	};
}

static auto
MakeUringSpliceIstream(struct pool &pool, EventLoop &event_loop, Uring::Queue &uring)
{
	return MakeUringSpliceIstream(pool, event_loop, uring, "build.ninja");
}

}

TEST(UringSpliceIstream, Basic)
try {
	TestInstance instance;
	instance.event_loop.EnableUring(1024, 0);
	auto &uring = *instance.event_loop.GetUring();

	auto [i, size] = MakeUringSpliceIstream(instance.root_pool, instance.event_loop, uring);

	{
		CountIstreamSink sink{std::move(i)};
		sink.EnableDirect();
		sink.Read();
		instance.event_loop.Run();
		EXPECT_TRUE(sink.IsDone());
		sink.RethrowError();
		EXPECT_EQ(sink.GetCount(), size);
	}
} catch (const std::system_error &e) {
	if (IsErrno(e, ENOSYS))
		GTEST_SKIP();
	else
		throw;
}

class DeferBreak {
	DeferEvent event;

public:
	explicit DeferBreak(EventLoop &event_loop) noexcept
		:event(event_loop, BIND_THIS_METHOD(Break)) {}

	void ScheduleIdle() noexcept {
		event.ScheduleIdle();
	}

	void ScheduleNext() noexcept {
		event.ScheduleNext();
	}

private:
	void Break() noexcept {
		event.GetEventLoop().Break();
	}
};

/**
 * Cancel before the io_uring splice operation was really submitted to
 * the kernel.
 */
TEST(UringSpliceIstream, CancelEarly)
try {
	TestInstance instance;
	DeferBreak defer_break{instance.event_loop};
	instance.event_loop.EnableUring(1024, 0);
	auto &uring = *instance.event_loop.GetUring();

	auto [i, size] = MakeUringSpliceIstream(instance.root_pool, instance.event_loop, uring);

	{
		CountIstreamSink sink{std::move(i)};
		sink.EnableDirect();
		sink.Read();
		defer_break.ScheduleIdle();
		instance.event_loop.Run();

		/* the io_uring splice operation is now on the ring,
		   but was not yet submitted via io_uring_submit() */

		EXPECT_FALSE(sink.IsDone());
		sink.RethrowError();
	}

	uring.DispatchCompletions();
} catch (const std::system_error &e) {
	if (IsErrno(e, ENOSYS))
		GTEST_SKIP();
	else
		throw;
}

/**
 * Cancel after the io_uring splice operation was submitted to the kernel.
 */
TEST(UringSpliceIstream, CancelLate)
try {
	TestInstance instance;
	DeferBreak defer_break{instance.event_loop};
	instance.event_loop.EnableUring(1024, 0);
	auto &uring = *instance.event_loop.GetUring();

	auto [i, size] = MakeUringSpliceIstream(instance.root_pool, instance.event_loop, uring);

	{
		CountIstreamSink sink{std::move(i)};
		sink.EnableDirect();
		sink.Read();
		defer_break.ScheduleNext();
		instance.event_loop.Run();

		/* the io_uring splice operation is now on the ring,
		   but was not yet submitted via io_uring_submit() */

		EXPECT_FALSE(sink.IsDone());
		sink.RethrowError();
	}

	uring.DispatchCompletions();
} catch (const std::system_error &e) {
	if (IsErrno(e, ENOSYS))
		GTEST_SKIP();
	else
		throw;
}

/**
 * Cancel one operation, possibly triggering a bug that clobbers the
 * second Istream's pipes.
 */
TEST(UringSpliceIstream, Clobber)
try {
	TestInstance instance;
	instance.event_loop.EnableUring(1024, 0);
	auto &uring = *instance.event_loop.GetUring();

	const char *const path = "build.ninja";
	const auto [fd, lease, size] = OpenFileLease(instance.root_pool, path);

	{
		CountIstreamSink sink{
			NewUringSpliceIstream(instance.event_loop, uring, nullptr, instance.root_pool,
					      path, fd, SharedLease{lease},
					      0, size),
		};

		sink.EnableDirect();
		sink.Read();

		DeferBreak defer_break{instance.event_loop};
		defer_break.ScheduleIdle();
		instance.event_loop.Run();

		/* the io_uring splice operation is now on the ring,
		   but was not yet submitted via io_uring_submit() */

		EXPECT_FALSE(sink.IsDone());
		sink.RethrowError();
	}

	/* the UringSpliceIstream has been destroyed, but the io_uring
	   splice operation may still be running; create another
	   UringSpliceIstream which may possibly reuse the old pipe
	   file descriptor numbers, and the old io_uring splice may
	   then accidently use these */

	{
		CountIstreamSink sink{
			NewUringSpliceIstream(instance.event_loop, uring, nullptr, instance.root_pool,
					      path, fd, SharedLease{lease},
					      0, size),
		};

		sink.EnableDirect();
		sink.Read();
		instance.event_loop.Run();
		EXPECT_TRUE(sink.IsDone());
		sink.RethrowError();
		EXPECT_EQ(sink.GetCount(), size);
	}

	uring.DispatchCompletions();
} catch (const std::system_error &e) {
	if (IsErrno(e, ENOSYS))
		GTEST_SKIP();
	else
		throw;
}
