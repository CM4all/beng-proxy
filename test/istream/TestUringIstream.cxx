// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "../TestInstance.hxx"
#include "istream/Sink.hxx"
#include "istream/UringIstream.hxx"
#include "istream/UnusedPtr.hxx"
#include "io/uring/Queue.hxx"
#include "io/Open.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "system/Error.hxx"

#include <gtest/gtest.h>

class MyHandler final : IstreamSink {
public:
	std::exception_ptr error;
	size_t got_data = 0;
	bool done = false;

	MyHandler(UnusedIstreamPtr _input) noexcept
		:IstreamSink(std::move(_input)) {}

	bool IsDone() const noexcept {
		return !HasInput();
	}

	void Read() noexcept {
		input.Read();
	}

	/* virtual methods from class IstreamHandler */

	size_t OnData(std::span<const std::byte> src) noexcept override {
		got_data += src.size();
		return src.size();
	}

	void OnEof() noexcept override {
		ClearInput();
	}

	void OnError(std::exception_ptr ep) noexcept override {
		ClearInput();
		error = std::move(ep);
	}
};

static std::pair<UnusedIstreamPtr, size_t>
MakeUringIstream(struct pool &pool, Uring::Queue &uring, const char *path)
{
	auto fd = OpenReadOnly(path);
	struct stat st;
	EXPECT_EQ(fstat(fd.Get(), &st), 0);

	return {NewUringIstream(uring, pool,
				path, std::move(fd),
				0, st.st_size),
		st.st_size};
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
		MyHandler h(std::move(i));
		h.Read();
		while (!h.IsDone())
			uring.WaitDispatchOneCompletion();

		EXPECT_EQ(h.got_data, size);
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
		MyHandler h(std::move(i));
		h.Read();
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
		MyHandler h(std::move(i));
		h.Read();

		while (!h.IsDone() && h.got_data == 0)
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
