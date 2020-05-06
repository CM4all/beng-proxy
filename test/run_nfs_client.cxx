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

#include "nfs/Handler.hxx"
#include "nfs/Client.hxx"
#include "nfs/Istream.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/AutoPipeIstream.hxx"
#include "istream/istream.hxx"
#include "istream/sink_fd.hxx"
#include "event/ShutdownListener.hxx"
#include "system/SetupProcess.hxx"
#include "io/FileDescriptor.hxx"
#include "io/SpliceSupport.hxx"
#include "PInstance.hxx"
#include "pool/pool.hxx"
#include "HttpResponseHandler.hxx"
#include "util/Cancellable.hxx"
#include "util/PrintException.hxx"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

struct Context final
	: PInstance, NfsClientHandler, NfsClientOpenFileHandler, SinkFdHandler
{
	PoolPtr pool;

	const char *path;

	ShutdownListener shutdown_listener;
	CancellablePointer cancel_ptr;

	NfsClient *client;

	bool aborted = false, failed = false, connected = false, closed = false;

	SinkFd *body;
	bool body_eof = false, body_abort = false, body_closed = false;

	Context()
		:shutdown_listener(event_loop, BIND_THIS_METHOD(ShutdownCallback)) {}

	void ShutdownCallback() noexcept;

	/* virtual methods from NfsClientHandler */
	void OnNfsClientReady(NfsClient &client) noexcept override;
	void OnNfsMountError(std::exception_ptr ep) noexcept override;
	void OnNfsClientClosed(std::exception_ptr ep) noexcept override;

	/* virtual methods from class NfsClientOpenFileHandler */
	void OnNfsOpen(NfsFileHandle *handle,
		       const struct statx &st) noexcept override;
	void OnNfsOpenError(std::exception_ptr ep) noexcept override;

	/* virtual methods from class SinkFdHandler */
	void OnInputEof() noexcept override;
	void OnInputError(std::exception_ptr ep) noexcept override;
	bool OnSendError(int error) noexcept override;
};

void
Context::ShutdownCallback() noexcept
{
	aborted = true;

	if (body != nullptr)
		sink_fd_close(body);
	else
		cancel_ptr.Cancel();

	if (client != nullptr)
		nfs_client_free(std::exchange(client, nullptr));
}

/*
 * sink_fd handler
 *
 */

void
Context::OnInputEof() noexcept
{
	body = nullptr;
	body_eof = true;

	shutdown_listener.Disable();
	nfs_client_free(client);
}

void
Context::OnInputError(std::exception_ptr ep) noexcept
{
	PrintException(ep);

	body = nullptr;
	body_abort = true;

	shutdown_listener.Disable();
	nfs_client_free(client);
}

bool
Context::OnSendError(int error) noexcept
{
	fprintf(stderr, "%s\n", strerror(error));

	body = nullptr;
	body_abort = true;

	shutdown_listener.Disable();
	nfs_client_free(client);
	return true;
}

/*
 * NfsClientOpenFileHandler
 *
 */

void
Context::OnNfsOpen(NfsFileHandle *handle, const struct statx &st) noexcept
{
	assert(!aborted);
	assert(!failed);
	assert(connected);

	body = sink_fd_new(event_loop, *pool,
			   NewAutoPipeIstream(pool,
					      istream_nfs_new(*pool, *handle,
							      0, st.stx_size),
					      nullptr),
			   FileDescriptor(STDOUT_FILENO),
			   guess_fd_type(STDOUT_FILENO),
			   *this);
	sink_fd_read(body);
}

void
Context::OnNfsOpenError(std::exception_ptr ep) noexcept
{
	assert(!aborted);
	assert(!failed);
	assert(connected);

	failed = true;

	PrintException(ep);

	shutdown_listener.Disable();
	nfs_client_free(client);
}

/*
 * nfs_client_handler
 *
 */

void
Context::OnNfsClientReady(NfsClient &_client) noexcept
{
	assert(!aborted);
	assert(!failed);
	assert(!connected);
	assert(!closed);

	connected = true;
	client = &_client;

	nfs_client_open_file(*client, path,
			     *this, cancel_ptr);
}

void
Context::OnNfsMountError(std::exception_ptr ep) noexcept
{
	assert(!aborted);
	assert(!failed);
	assert(!connected);
	assert(!closed);

	failed = true;

	PrintException(ep);

	shutdown_listener.Disable();
}

void
Context::OnNfsClientClosed(std::exception_ptr ep) noexcept
{
	assert(!aborted);
	assert(!failed);
	assert(connected);
	assert(!closed);

	closed = true;

	PrintException(ep);
}

/*
 * main
 *
 */

int
main(int argc, char **argv)
{
	if (argc != 4) {
		fprintf(stderr, "usage: run_nfs_client SERVER ROOT PATH\n");
		return EXIT_FAILURE;
	}

	const char *const server = argv[1];
	const char *const _export = argv[2];

	Context ctx;
	ctx.path = argv[3];

	/* initialize */

	SetupProcess();

	direct_global_init();

	ctx.shutdown_listener.Enable();

	ctx.pool = pool_new_libc(ctx.root_pool, "pool");

	/* open NFS connection */

	nfs_client_new(ctx.event_loop, server, _export,
		       ctx, ctx.cancel_ptr);

	/* run */

	ctx.event_loop.Dispatch();

	assert(ctx.aborted || ctx.failed || ctx.connected);

	/* cleanup */

	return ctx.connected
		? EXIT_SUCCESS
		: EXIT_FAILURE;
}
