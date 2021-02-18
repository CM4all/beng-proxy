/*
 * Copyright 2007-2021 CM4all GmbH
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

#include "DemoHttpServerConnection.hxx"
#include "duplex.hxx"
#include "PInstance.hxx"
#include "pool/pool.hxx"
#include "pool/Holder.hxx"
#include "pool/UniquePtr.hxx"
#include "event/ShutdownListener.hxx"
#include "event/net/TemplateServerSocket.hxx"
#include "fs/FilteredSocket.hxx"
#include "fb_pool.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "io/SpliceSupport.hxx"
#include "util/PrintException.hxx"

#include <memory>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Instance;

class Connection final
	: public AutoUnlinkIntrusiveListHook,
	  PoolHolder,
	  DemoHttpServerConnection
{
	Instance &instance;

public:
	Connection(Instance &instance, Mode _mode,
		   UniqueSocketDescriptor &&_fd,
		   SocketAddress address) noexcept;

protected:
	/* virtual methods from class HttpServerConnectionHandler */
	void HttpConnectionError(std::exception_ptr e) noexcept override;
	void HttpConnectionClosed() noexcept override;
};

using Listener = TemplateServerSocket<Connection, Instance &,
				      DemoHttpServerConnection::Mode>;

struct Instance final : PInstance {
	ShutdownListener shutdown_listener;

	std::unique_ptr<Listener> listener;

	Instance()
		:shutdown_listener(event_loop, BIND_THIS_METHOD(ShutdownCallback)) {}

	void OnConnectionClosed() noexcept;

private:
	void ShutdownCallback() noexcept;
};

Connection::Connection(Instance &_instance, Mode _mode,
		       UniqueSocketDescriptor &&fd,
		       SocketAddress address) noexcept
	:PoolHolder(pool_new_linear(_instance.root_pool, "connection", 2048)),
	 DemoHttpServerConnection(pool, _instance.event_loop,
				  UniquePoolPtr<FilteredSocket>::Make(pool,
								      _instance.event_loop,
								      std::move(fd),
								      FdType::FD_SOCKET),
				  address, _mode),
	 instance(_instance)
{
}

void
Instance::ShutdownCallback() noexcept
{
	listener.reset();
}

void
Instance::OnConnectionClosed() noexcept
{
	if (!listener)
		shutdown_listener.Disable();
}

void
Connection::HttpConnectionError(std::exception_ptr e) noexcept
{
	DemoHttpServerConnection::HttpConnectionError(std::move(e));

	instance.OnConnectionClosed();
	delete this;
}

void
Connection::HttpConnectionClosed() noexcept
{
	DemoHttpServerConnection::HttpConnectionClosed();
	instance.OnConnectionClosed();
	delete this;
}

/*
 * main
 *
 */

int
main(int argc, char **argv)
try {
	if (argc != 4) {
		fprintf(stderr, "Usage: %s INFD OUTFD {null|mirror|close|dummy|fixed|huge|hold}\n", argv[0]);
		return EXIT_FAILURE;
	}

	UniqueSocketDescriptor listen_fd;
	int in_fd = -1, out_fd = -1;

	if (strcmp(argv[1], "accept") == 0) {
		listen_fd = UniqueSocketDescriptor(atoi(argv[2]));
	} else {
		in_fd = atoi(argv[1]);
		out_fd = atoi(argv[2]);
	}

	direct_global_init();
	const ScopeFbPoolInit fb_pool_init;

	Instance instance;
	instance.shutdown_listener.Enable();

	const char *mode = argv[3];
	DemoHttpServerConnection::Mode parsed_mode;
	if (strcmp(mode, "null") == 0)
		parsed_mode = DemoHttpServerConnection::Mode::MODE_NULL;
	else if (strcmp(mode, "mirror") == 0)
		parsed_mode = DemoHttpServerConnection::Mode::MIRROR;
	else if (strcmp(mode, "close") == 0)
		parsed_mode = DemoHttpServerConnection::Mode::CLOSE;
	else if (strcmp(mode, "dummy") == 0)
		parsed_mode = DemoHttpServerConnection::Mode::DUMMY;
	else if (strcmp(mode, "fixed") == 0)
		parsed_mode = DemoHttpServerConnection::Mode::FIXED;
	else if (strcmp(mode, "huge") == 0)
		parsed_mode = DemoHttpServerConnection::Mode::HUGE_;
	else if (strcmp(mode, "hold") == 0)
		parsed_mode = DemoHttpServerConnection::Mode::HOLD;
	else if (strcmp(mode, "nop") == 0)
		parsed_mode = DemoHttpServerConnection::Mode::NOP;
	else if (strcmp(mode, "failing-keepalive") == 0)
		parsed_mode = DemoHttpServerConnection::Mode::FAILING_KEEPALIVE;
	else {
		fprintf(stderr, "Unknown mode: %s\n", mode);
		return EXIT_FAILURE;
	}

	if (listen_fd.IsDefined()) {
		instance.listener = std::make_unique<Listener>(instance.event_loop,
							       instance, parsed_mode);
		instance.listener->Listen(std::move(listen_fd));
	} else {
		UniqueSocketDescriptor sockfd;
		if (in_fd != out_fd) {
			sockfd = duplex_new(instance.event_loop, instance.root_pool,
					    UniqueFileDescriptor(in_fd),
					    UniqueFileDescriptor(out_fd));
		} else
			sockfd = UniqueSocketDescriptor(in_fd);

		new Connection(instance, parsed_mode, std::move(sockfd), {});
	}

	instance.event_loop.Dispatch();

	return EXIT_SUCCESS;
} catch (...) {
	PrintException(std::current_exception());
	return EXIT_FAILURE;
}
