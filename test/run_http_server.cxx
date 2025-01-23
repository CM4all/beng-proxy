// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "DemoHttpServerConnection.hxx"
#include "TestInstance.hxx"
#include "pool/pool.hxx"
#include "pool/Holder.hxx"
#include "pool/UniquePtr.hxx"
#include "event/ShutdownListener.hxx"
#include "event/net/TemplateServerSocket.hxx"
#include "fs/FilteredSocket.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "io/UniqueFileDescriptor.hxx"
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

struct Instance final : TestInstance {
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
	if (argc != 2) {
		fprintf(stderr, "Usage: %s {null|mirror|close|dummy|fixed|huge|hold}\n", argv[0]);
		return EXIT_FAILURE;
	}

	UniqueSocketDescriptor listen_fd{AdoptTag{}, STDIN_FILENO};

	Instance instance;
	instance.shutdown_listener.Enable();

	const char *mode = argv[1];
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

	instance.listener = std::make_unique<Listener>(instance.event_loop,
						       instance, parsed_mode);
	instance.listener->Listen(std::move(listen_fd));

	instance.event_loop.Run();

	return EXIT_SUCCESS;
} catch (...) {
	PrintException(std::current_exception());
	return EXIT_FAILURE;
}
