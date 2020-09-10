/*
 * Copyright 2007-2020 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "istream/Sink.hxx"
#include "istream/UringIstream.hxx"
#include "istream/UnusedPtr.hxx"
#include "io/uring/Queue.hxx"
#include "io/Open.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "system/Error.hxx"
#include "pool/RootPool.hxx"
#include "fb_pool.hxx"

#include <gtest/gtest.h>

class MyHandler final : IstreamSink {
public:
	std::exception_ptr error;
	size_t got_data = 0;
	bool done = false;

	MyHandler(UnusedIstreamPtr _input) noexcept
		:IstreamSink(std::move(_input)) {}

	~MyHandler() noexcept {
		if (HasInput())
			ClearAndCloseInput();
	}

	bool IsDone() const noexcept {
		return !HasInput();
	}

	void Read() noexcept {
		input.Read();
	}

	/* virtual methods from class IstreamHandler */

	size_t OnData(const void *, size_t length) noexcept override {
		got_data += length;
		return length;
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
	const ScopeFbPoolInit fb_pool_init;
	RootPool root_pool;
	Uring::Queue uring(1024, 0);

	auto [i, size] = MakeUringIstream(root_pool, uring);

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
	const ScopeFbPoolInit fb_pool_init;
	RootPool root_pool;
	Uring::Queue uring(1024, 0);

	auto [i, size] = MakeUringIstream(root_pool, uring);

	{
		MyHandler h(std::move(i));
		h.Read();
	}

	uring.DispatchCompletions();
} catch (const std::system_error &e) {
	if (IsErrno(e, ENOSYS))
		GTEST_SKIP();
	else
		throw;
}

TEST(UringIstream, CancelLate)
try {
	const ScopeFbPoolInit fb_pool_init;
	RootPool root_pool;
	Uring::Queue uring(1024, 0);

	auto [i, size] = MakeUringIstream(root_pool, uring);

	{
		MyHandler h(std::move(i));
		h.Read();

		while (!h.IsDone() && h.got_data == 0)
			uring.WaitDispatchOneCompletion();
	}

	uring.DispatchCompletions();
} catch (const std::system_error &e) {
	if (IsErrno(e, ENOSYS))
		GTEST_SKIP();
	else
		throw;
}
