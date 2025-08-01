// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "../TestInstance.hxx"
#include "../OpenFileLease.hxx"
#include "CountIstreamSink.hxx"
#include "istream/UringIstream.hxx"
#include "istream/UnusedPtr.hxx"
#include "pool/pool.hxx"
#include "io/uring/Queue.hxx"
#include "system/Error.hxx"

#include <gtest/gtest.h>

static std::pair<UnusedIstreamPtr, size_t>
MakeUringIstream(struct pool &pool, Uring::Queue &uring, const char *path)
{
	auto [fd, lease, size] = OpenFileLease(path);

	return {
		NewUringIstream(uring, pool,
				path, fd, std::move(lease),
				0, size),
		size,
	};
}

static auto
MakeUringIstream(struct pool &pool, Uring::Queue &uring)
{
	return MakeUringIstream(pool, uring, "build.ninja");
}

TEST(UringIstream, Basic)
try {
	TestInstance instance;
	Uring::Queue uring(1024, 0);

	auto [i, size] = MakeUringIstream(instance.root_pool, uring);

	{
		CountIstreamSink sink{std::move(i)};
		sink.Read();
		while (!sink.IsDone())
			uring.WaitDispatchOneCompletion();

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

TEST(UringIstream, Cancel)
try {
	TestInstance instance;
	Uring::Queue uring(1024, 0);

	auto [i, size] = MakeUringIstream(instance.root_pool, uring);

	{
		CountIstreamSink sink{std::move(i)};
		sink.Read();
	}

	uring.DispatchCompletions();

	// TODO: fix this race properly
	if (uring.HasPending())
		uring.WaitDispatchOneCompletion();
} catch (const std::system_error &e) {
	if (IsErrno(e, ENOSYS))
		GTEST_SKIP();
	else
		throw;
}

TEST(UringIstream, CancelLate)
try {
	TestInstance instance;
	Uring::Queue uring(1024, 0);

	auto [i, size] = MakeUringIstream(instance.root_pool, uring);

	{
		CountIstreamSink sink{std::move(i)};
		sink.Read();

		while (!sink.IsDone() && sink.GetCount() == 0)
			uring.WaitDispatchOneCompletion();
	}

	uring.DispatchCompletions();

	// TODO: fix this race properly
	if (uring.HasPending())
	    uring.WaitDispatchOneCompletion();
} catch (const std::system_error &e) {
	if (IsErrno(e, ENOSYS))
		GTEST_SKIP();
	else
		throw;
}
