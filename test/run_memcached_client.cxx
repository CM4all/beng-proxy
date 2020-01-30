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

#include "memcached/Client.hxx"
#include "memcached/Handler.hxx"
#include "lease.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/AutoPipeIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/sink_fd.hxx"
#include "PInstance.hxx"
#include "event/ShutdownListener.hxx"
#include "fb_pool.hxx"
#include "pool/pool.hxx"
#include "pool/Ptr.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "net/RConnectSocket.hxx"
#include "system/SetupProcess.hxx"
#include "io/FileDescriptor.hxx"
#include "io/SpliceSupport.hxx"
#include "util/ByteOrder.hxx"
#include "util/Cancellable.hxx"
#include "util/PrintException.hxx"

#include <unistd.h>
#include <stdio.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>

struct Context final
	: PInstance, Lease, MemcachedResponseHandler, SinkFdHandler
{
	ShutdownListener shutdown_listener;

	PoolPtr pool;

	CancellablePointer cancel_ptr;

	UniqueSocketDescriptor s;
	bool idle = false, reuse, aborted = false;
	enum memcached_response_status status;

	SinkFd *value;
	bool value_eof = false, value_abort = false, value_closed = false;

	Context()
		:shutdown_listener(event_loop, BIND_THIS_METHOD(ShutdownCallback)),
		 pool(pool_new_linear(root_pool, "test", 8192)) {}

	void ShutdownCallback() noexcept;

	/* virtual methods from class Lease */
	void ReleaseLease(bool _reuse) noexcept override {
		assert(!idle);
		assert(s.IsDefined());

		idle = true;
		reuse = _reuse;

		s.Close();
	}

	/* virtual methods from class MemcachedResponseHandler */
	void OnMemcachedResponse(enum memcached_response_status status,
				 const void *extras, size_t extras_length,
				 const void *key, size_t key_length,
				 UnusedIstreamPtr value) noexcept override;
	void OnMemcachedError(std::exception_ptr ep) noexcept override;

	/* virtual methods from class SinkFdHandler */
	void OnInputEof() noexcept;
	void OnInputError(std::exception_ptr ep) noexcept;
	bool OnSendError(int error) noexcept;
};

void
Context::ShutdownCallback() noexcept
{
	if (value != nullptr) {
		sink_fd_close(value);
		value = nullptr;
		value_abort = true;
	} else {
		aborted = true;
		cancel_ptr.Cancel();
	}
}

/*
 * sink_fd handler
 *
 */

void
Context::OnInputEof() noexcept
{
	value = nullptr;
	value_eof = true;

	shutdown_listener.Disable();
}

void
Context::OnInputError(std::exception_ptr ep) noexcept
{
	PrintException(ep);

	value = nullptr;
	value_abort = true;

	shutdown_listener.Disable();
}

bool
Context::OnSendError(int error) noexcept
{
	fprintf(stderr, "%s\n", strerror(error));

	value = nullptr;
	value_abort = true;

	shutdown_listener.Disable();
	return true;
}

/*
 * memcached_response_handler_t
 *
 */

void
Context::OnMemcachedResponse(enum memcached_response_status _status,
			     const void *, size_t,
			     const void *, size_t,
			     UnusedIstreamPtr _value) noexcept
{
	fprintf(stderr, "status=%d\n", _status);

	status = _status;

	if (_value) {
		value = sink_fd_new(event_loop, *pool,
				    NewAutoPipeIstream(pool, std::move(_value),
						       nullptr),
				    FileDescriptor(STDOUT_FILENO),
				    guess_fd_type(STDOUT_FILENO),
				    *this);
	} else {
		value_eof = true;
		shutdown_listener.Disable();
	}
}

void
Context::OnMemcachedError(std::exception_ptr ep) noexcept
{
	PrintException(ep);

	status = (memcached_response_status)-1;
	value_eof = true;

	shutdown_listener.Disable();
}

/*
 * main
 *
 */

int main(int argc, char **argv) {
	enum memcached_opcode opcode;
	const char *key, *value;
	const void *extras;
	size_t extras_length;
	struct memcached_set_extras set_extras;

	if (argc < 3 || argc > 5) {
		fprintf(stderr, "usage: run-memcached-client HOST[:PORT] OPCODE [KEY] [VALUE]\n");
		return 1;
	}

	if (strcmp(argv[2], "get") == 0)
		opcode = MEMCACHED_OPCODE_GET;
	else if (strcmp(argv[2], "set") == 0)
		opcode = MEMCACHED_OPCODE_SET;
	else if (strcmp(argv[2], "delete") == 0)
		opcode = MEMCACHED_OPCODE_DELETE;
	else {
		fprintf(stderr, "unknown opcode\n");
		return 1;
	}

	key = argc > 3 ? argv[3] : NULL;
	value = argc > 4 ? argv[4] : NULL;

	if (opcode == MEMCACHED_OPCODE_SET) {
		set_extras.flags = 0;
		set_extras.expiration = ToBE32(300);
		extras = &set_extras;
		extras_length = sizeof(set_extras);
	} else {
		extras = NULL;
		extras_length = 0;
	}

	direct_global_init();

	/* connect socket */

	Context ctx;
	ctx.s = ResolveConnectStreamSocket(argv[1], 11211);
	ctx.s.SetNoDelay();

	/* initialize */

	SetupProcess();
	const ScopeFbPoolInit fb_pool_init;

	ctx.shutdown_listener.Enable();

	/* run test */

	memcached_client_invoke(ctx.pool, ctx.event_loop, ctx.s, FdType::FD_TCP,
				ctx,
				opcode,
				extras, extras_length,
				key, key != NULL ? strlen(key) : 0,
				value != nullptr ? istream_string_new(ctx.pool, value) : nullptr,
				ctx, ctx.cancel_ptr);

	ctx.event_loop.Dispatch();

	assert(ctx.value_eof || ctx.value_abort || ctx.aborted);

	/* cleanup */

	ctx.pool.reset();
	pool_commit();

	return ctx.value_eof ? 0 : 2;
}
