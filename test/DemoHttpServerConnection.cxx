// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "DemoHttpServerConnection.hxx"
#include "http/IncomingRequest.hxx"
#include "http/Headers.hxx"
#include "http/Status.hxx"
#include "http/server/Public.hxx"
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
					       request_slice_pool,
					       *this, *this)),
	 response_timer(event_loop, BIND_THIS_METHOD(OnResponseTimer)),
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
DemoHttpServerConnection::OnResponseTimer() noexcept
{
	const HttpStatus status = request_body
		? HttpStatus::OK
		: HttpStatus::NO_CONTENT;

	current_request->SendResponse(status, {}, std::move(request_body));
}

void
DemoHttpServerConnection::HandleHttpRequest(IncomingHttpRequest &request,
						   const StopwatchPtr &,
						   CancellablePointer &cancel_ptr) noexcept
{
	switch (mode) {
		HttpStatus status;
		static constexpr std::byte data[0x100]{};

	case Mode::MODE_NULL:
		if (request.body)
			sink_null_new(request.pool, std::move(request.body));

		request.SendResponse(HttpStatus::NO_CONTENT, {}, nullptr);
		break;

	case Mode::MIRROR:
		status = request.body
			? HttpStatus::OK
			: HttpStatus::NO_CONTENT;
		request.SendResponse(status, {}, std::move(request.body));
		break;

	case Mode::DEFER_MIRROR:
		current_request = &request;
		request_body = UnusedHoldIstreamPtr(request.pool,
						    std::move(request.body));
		response_timer.ScheduleIdle();
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

			request.SendResponse(HttpStatus::OK, {}, std::move(body));
		}

		break;

	case Mode::FIXED:
		if (request.body)
			sink_null_new(request.pool, std::move(request.body));

		request.SendResponse(HttpStatus::OK, {},
				     istream_memory_new(request.pool,
							data));
		break;

	case Mode::HUGE_:
		if (request.body)
			sink_null_new(request.pool, std::move(request.body));

		request.SendResponse(HttpStatus::OK, {},
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

			request.SendResponse(HttpStatus::OK, {},
					     std::move(delayed.first));
		}

		break;

	case Mode::BLOCK:
		request_body = UnusedHoldIstreamPtr(request.pool,
						    std::move(request.body));
		request.SendResponse(HttpStatus::OK, {},
				     istream_block_new(request.pool));
		break;

	case Mode::NOP:
		cancel_ptr = *this;
		break;

	case Mode::FAILING_KEEPALIVE:
		if (first) {
			first = false;
			request.SendResponse(HttpStatus::OK, {},
					     istream_memory_new(request.pool,
								data));
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
