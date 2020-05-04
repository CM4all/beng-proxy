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

#include "istream/UringIstream.hxx"
#include "istream/sink_fd.hxx"
#include "istream/UnusedPtr.hxx"
#include "system/Error.hxx"
#include "io/Open.hxx"
#include "io/SpliceSupport.hxx"
#include "event/uring/Manager.hxx"
#include "util/PrintException.hxx"
#include "fb_pool.hxx"
#include "PInstance.hxx"

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>

struct Context final : PInstance, SinkFdHandler {
	Uring::Manager uring_manager;

	SinkFd *sink = nullptr;
	std::exception_ptr error;

	Context():uring_manager(event_loop) {}

	void BeginShutdown() noexcept {
		uring_manager.SetVolatile();
	}

	/* virtual methods from class SinkFdHandler */
	void OnInputEof() noexcept override;
	void OnInputError(std::exception_ptr ep) noexcept override;
	bool OnSendError(int error) noexcept override;
};

void
Context::OnInputEof() noexcept
{
	sink = nullptr;
	BeginShutdown();
}

void
Context::OnInputError(std::exception_ptr ep) noexcept
{
	sink = nullptr;
	error = std::move(ep);
	BeginShutdown();
}

bool
Context::OnSendError(int _error) noexcept
{
	sink = nullptr;
	error = std::make_exception_ptr(MakeErrno(_error, "Failed to write"));
	BeginShutdown();

	return true;
}

int
main(int argc, char **argv)
try {
	if (argc != 2) {
		fprintf(stderr, "Usage: %s PATH\n", argv[0]);
		return EXIT_FAILURE;
	}

	const char *path = argv[1];

	direct_global_init();

	const ScopeFbPoolInit fb_pool_init;

	Context context;

	auto fd = OpenReadOnly(path);
	struct stat st;
	if (fstat(fd.Get(), &st) < 0)
		throw FormatErrno("Failed to stat %s", path);

	if (!S_ISREG(st.st_mode))
		throw std::runtime_error("Not a regular file");

	context.sink = sink_fd_new(context.event_loop, context.root_pool,
				   NewUringIstream(context.uring_manager,
						   context.root_pool, path,
						   std::move(fd),
						   0, st.st_size),
				   FileDescriptor(STDOUT_FILENO),
				   guess_fd_type(STDOUT_FILENO),
				   context);

	context.event_loop.Dispatch();

	if (context.error)
		std::rethrow_exception(context.error);

	return EXIT_SUCCESS;
} catch (...) {
	PrintException(std::current_exception());
	return EXIT_FAILURE;
}
