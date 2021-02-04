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
#include "http/IncomingRequest.hxx"
#include "http/Headers.hxx"
#include "http_server/http_server.hxx"
#include "fs/FilteredSocket.hxx"
#include "istream/sink_null.hxx"
#include "istream/BlockIstream.hxx"
#include "istream/ByteIstream.hxx"
#include "istream/DelayedIstream.hxx"
#include "istream/HeadIstream.hxx"
#include "istream/istream_hold.hxx"
#include "istream/istream_memory.hxx"
#include "istream/ZeroIstream.hxx"
#include "istream/istream.hxx"
#include "pool/UniquePtr.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "util/PrintException.hxx"

DemoHttpServerConnection::DemoHttpServerConnection(struct pool &pool,
						   EventLoop &event_loop,
						   UniquePoolPtr<FilteredSocket> socket,
						   SocketAddress address,
						   Mode _mode) noexcept
	:connection(http_server_connection_new(pool,
					       std::move(socket),
					       nullptr,
					       address,
					       true,
					       *this)),
	 defer_event(event_loop, BIND_THIS_METHOD(OnDeferred)),
	 mode(_mode) {}

DemoHttpServerConnection::~DemoHttpServerConnection() noexcept
{
	if (connection != nullptr)
		http_server_connection_close(connection);
}

void
DemoHttpServerConnection::OnDeferred() noexcept
{
	if (connection != nullptr)
		http_server_connection_close(connection);

	HttpConnectionClosed();
}

void
DemoHttpServerConnection::HandleHttpRequest(IncomingHttpRequest &request,
						   const StopwatchPtr &,
						   CancellablePointer &cancel_ptr) noexcept
{
	switch (mode) {
		http_status_t status;
		static char data[0x100];

	case Mode::MODE_NULL:
		if (request.body)
			sink_null_new(request.pool, std::move(request.body));

		request.SendResponse(HTTP_STATUS_NO_CONTENT, {}, nullptr);
		break;

	case Mode::MIRROR:
		status = request.body
			? HTTP_STATUS_OK
			: HTTP_STATUS_NO_CONTENT;
		request.SendResponse(status, {}, std::move(request.body));
		break;

	case Mode::CLOSE:
		/* disable keep-alive */
		http_server_connection_graceful(connection);

		/* fall through */

	case Mode::DUMMY:
		if (request.body)
			sink_null_new(request.pool, std::move(request.body));

		{
			auto body = istream_head_new(request.pool,
						     istream_zero_new(request.pool),
						     256, false);
			body = istream_byte_new(request.pool, std::move(body));

			request.SendResponse(HTTP_STATUS_OK, {}, std::move(body));
		}

		break;

	case Mode::FIXED:
		if (request.body)
			sink_null_new(request.pool, std::move(request.body));

		request.SendResponse(HTTP_STATUS_OK, {},
				     istream_memory_new(request.pool,
							data, sizeof(data)));
		break;

	case Mode::HUGE_:
		if (request.body)
			sink_null_new(request.pool, std::move(request.body));

		request.SendResponse(HTTP_STATUS_OK, {},
				     istream_head_new(request.pool,
						      istream_zero_new(request.pool),
						      512 * 1024, true));
		break;

	case Mode::HOLD:
		request_body = UnusedHoldIstreamPtr(request.pool,
						    std::move(request.body));

		{
			auto delayed = istream_delayed_new(request.pool,
							   GetEventLoop());
			delayed.second.cancel_ptr = *this;

			request.SendResponse(HTTP_STATUS_OK, {},
					     std::move(delayed.first));
		}

		defer_event.ScheduleIdle();
		break;

	case Mode::BLOCK:
		request_body = UnusedHoldIstreamPtr(request.pool,
						    std::move(request.body));
		request.SendResponse(HTTP_STATUS_OK, {},
				     istream_block_new(request.pool));
		break;

	case Mode::NOP:
		cancel_ptr = *this;
		break;

	case Mode::FAILING_KEEPALIVE:
		if (first) {
			first = false;
			request.SendResponse(HTTP_STATUS_OK, {},
					     istream_memory_new(request.pool,
								data, sizeof(data)));
		} else {
			http_server_connection_close(connection);
			HttpConnectionClosed();
		}

		break;
	}
}

void
DemoHttpServerConnection::HttpConnectionError(std::exception_ptr e) noexcept
{
	connection = nullptr;

	PrintException(e);
}

void
DemoHttpServerConnection::HttpConnectionClosed() noexcept
{
	connection = nullptr;
}
