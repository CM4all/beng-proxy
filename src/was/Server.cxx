// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Server.hxx"
#include "Map.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/istream_null.hxx"
#include "strmap.hxx"
#include "lib/fmt/ToBuffer.hxx"
#include "http/CommonHeaders.hxx"
#include "http/Method.hxx"
#include "http/Status.hxx"
#include "pool/pool.hxx"
#include "net/SocketProtocolError.hxx"
#include "util/SpanCast.hxx"
#include "util/StringSplit.hxx"
#include "util/Unaligned.hxx"
#include "AllocatorPtr.hxx"

#include <was/protocol.h>

#include <unistd.h>

WasServer::WasServer(struct pool &_pool, EventLoop &event_loop,
		     WasSocket &&_socket,
		     WasServerHandler &_handler) noexcept
	:pool(_pool),
	 socket(std::move(_socket)),
	 control(event_loop, socket.control, *this),
	 handler(_handler)
{
	assert(socket.control.IsDefined());
	assert(socket.input.IsDefined());
	assert(socket.output.IsDefined());
}

void
WasServer::ReleaseError(std::exception_ptr ep) noexcept
{
	if (control.IsDefined())
		control.ReleaseSocket();

	if (request.state != Request::State::NONE) {
		if (request.body != nullptr)
			was_input_free_p(&request.body, ep);

		if (request.state == Request::State::SUBMITTED &&
		    response.body != nullptr)
			was_output_free_p(&response.body);

		request.pool.reset();
	}

	Destroy();
}

void
WasServer::ReleaseError(const char *msg) noexcept
{
	ReleaseError(std::make_exception_ptr(SocketProtocolError{msg}));
}

void
WasServer::ReleaseUnused() noexcept
{
	if (control.IsDefined())
		control.ReleaseSocket();

	if (request.state != Request::State::NONE) {
		if (request.body != nullptr)
			was_input_free_unused_p(&request.body);

		if (request.state == Request::State::SUBMITTED &&
		    response.body != nullptr)
			was_output_free_p(&response.body);

		request.pool.reset();
	}

	Destroy();
}

void
WasServer::AbortError(std::exception_ptr ep) noexcept
{
	auto &handler2 = handler;
	ReleaseError(ep);
	handler2.OnWasClosed();
}

void
WasServer::AbortProtocolError(const char *msg) noexcept
{
	AbortError(std::make_exception_ptr(SocketProtocolError{msg}));
}

void
WasServer::AbortUnused() noexcept
{
	auto &handler2 = handler;
	ReleaseUnused();
	handler2.OnWasClosed();
}

/*
 * Output handler
 */

bool
WasServer::WasOutputLength(uint64_t length) noexcept
{
	assert(control.IsDefined());
	assert(response.body != nullptr);

	return control.SendUint64(WAS_COMMAND_LENGTH, length);
}

bool
WasServer::WasOutputPremature(uint64_t length, std::exception_ptr ep) noexcept
{
	assert(response.body != nullptr);
	response.body = nullptr;

	if (!control.IsDefined())
		/* this can happen if was_input_free() call destroys the
		   WasOutput instance; this check means to work around this
		   circular call */
		return true;

	(void)ep; // TODO: log?

	return control.SendUint64(WAS_COMMAND_PREMATURE, length);
}

void
WasServer::WasOutputEof() noexcept
{
	assert(response.body != nullptr);

	response.body = nullptr;
}

void
WasServer::WasOutputError(std::exception_ptr ep) noexcept
{
	assert(response.body != nullptr);

	response.body = nullptr;
	AbortError(ep);
}

/*
 * Input handler
 */

void
WasServer::WasInputClose([[maybe_unused]] uint64_t received) noexcept
{
	/* this happens when the request handler isn't interested in the
	   request body */

	assert(request.state == Request::State::SUBMITTED);
	assert(request.body != nullptr);

	request.body = nullptr;

	if (control.IsDefined())
		control.Send(WAS_COMMAND_STOP);

	// TODO: handle PREMATURE packet which we'll receive soon
}

bool
WasServer::WasInputRelease() noexcept
{
	assert(request.body != nullptr);
	assert(!request.released);

	request.released = true;
	return true;
}

void
WasServer::WasInputEof() noexcept
{
	assert(request.state == Request::State::SUBMITTED);
	assert(request.body != nullptr);
	assert(request.released);

	request.body = nullptr;

	// TODO
}

void
WasServer::WasInputError() noexcept
{
	assert(request.state == Request::State::SUBMITTED);
	assert(request.body != nullptr);

	request.body = nullptr;

	AbortUnused();
}

/*
 * Control channel handler
 */

bool
WasServer::OnWasControlPacket(enum was_command cmd,
			      std::span<const std::byte> payload) noexcept
{
	switch (cmd) {
		HttpMethod method;

	case WAS_COMMAND_NOP:
	case WAS_COMMAND_METRIC:
		break;

	case WAS_COMMAND_REQUEST:
		if (request.state != Request::State::NONE) {
			AbortProtocolError("misplaced REQUEST packet");
			return false;
		}

		request.pool = pool_new_linear(&pool, "was_server_request", 32768);
		request.method = HttpMethod::GET;
		request.uri = nullptr;
		request.headers = strmap_new(request.pool);
		request.body = nullptr;
		request.state = Request::State::HEADERS;
		response.body = nullptr;
		break;

	case WAS_COMMAND_METHOD:
		if (request.state != Request::State::HEADERS) {
			AbortProtocolError("misplaced METHOD packet");
			return false;
		}

		if (payload.size() != sizeof(uint32_t)) {
			AbortProtocolError("malformed METHOD packet");
			return false;
		}

		method = static_cast<HttpMethod>(*(const uint32_t *)(const void *)payload.data());
		if (request.method != HttpMethod::GET &&
		    method != request.method) {
			/* sending that packet twice is illegal */
			AbortProtocolError("misplaced METHOD packet");
			return false;
		}

		if (!http_method_is_valid(method)) {
			AbortProtocolError("invalid METHOD packet");
			return false;
		}

		request.method = method;
		break;

	case WAS_COMMAND_URI:
		if (request.state != Request::State::HEADERS ||
		    request.uri != nullptr) {
			AbortProtocolError("misplaced URI packet");
			return false;
		}

		request.uri = p_strndup(request.pool,
					(const char *)payload.data(),
					payload.size());
		break;

	case WAS_COMMAND_SCRIPT_NAME:
	case WAS_COMMAND_PATH_INFO:
	case WAS_COMMAND_QUERY_STRING:
	case WAS_COMMAND_REMOTE_HOST:
		// XXX
		break;

	case WAS_COMMAND_HEADER:
		if (request.state != Request::State::HEADERS) {
			AbortProtocolError("misplaced HEADER packet");
			return false;
		}

		if (auto [name, value] = Split(ToStringView(payload), '=');
		    value.data() != nullptr) {
			// TODO
		} else {
			AbortProtocolError("malformed HEADER packet");
			return false;
		}

		break;

	case WAS_COMMAND_PARAMETER:
		if (request.state != Request::State::HEADERS) {
			AbortProtocolError("misplaced PARAMETER packet");
			return false;
		}

		if (auto [name, value] = Split(ToStringView(payload), '=');
		    value.data() != nullptr) {
			// TODO
		} else {
			AbortProtocolError("malformed PARAMETER packet");
			return false;
		}

		break;

	case WAS_COMMAND_STATUS:
		AbortProtocolError("misplaced STATUS packet");
		return false;

	case WAS_COMMAND_NO_DATA:
		if (request.state != Request::State::HEADERS ||
		    request.uri == nullptr) {
			AbortProtocolError("misplaced NO_DATA packet");
			return false;
		}

		request.body = nullptr;
		request.state = Request::State::PENDING;
		break;

	case WAS_COMMAND_DATA:
		if (request.state != Request::State::HEADERS ||
		    request.uri == nullptr) {
			AbortProtocolError("misplaced DATA packet");
			return false;
		}

		request.body = was_input_new(*request.pool, control.GetEventLoop(),
					     socket.input, *this);
		request.state = Request::State::PENDING;
		break;

	case WAS_COMMAND_LENGTH:
		if (request.state < Request::State::PENDING ||
		    request.body == nullptr) {
			AbortProtocolError("misplaced LENGTH packet");
			return false;
		}

		if (payload.size() != sizeof(uint64_t)) {
			AbortProtocolError("malformed LENGTH packet");
			return false;
		}

		if (!was_input_set_length(request.body,
					  LoadUnaligned<uint64_t>(payload.data()))) {
			AbortProtocolError("invalid LENGTH packet");
			return false;
		}

		break;

	case WAS_COMMAND_STOP:
		// XXX
		AbortProtocolError(FmtBuffer<64>("unexpected packet: {}",
						 static_cast<unsigned>(cmd)));
		return false;

	case WAS_COMMAND_PREMATURE:
		if (payload.size() != sizeof(uint64_t)) {
			AbortError(std::make_exception_ptr("malformed PREMATURE packet"));
			return false;
		}

		if (request.body == nullptr)
			break;

		was_input_premature(request.body,
				    LoadUnaligned<uint64_t>(payload.data()));
		return false;
	}

	return true;
}

bool
WasServer::OnWasControlDrained() noexcept
{
	if (request.state == Request::State::PENDING) {
		request.state = Request::State::SUBMITTED;

		UnusedIstreamPtr body;
		if (request.released) {
			was_input_free_unused(request.body);
			request.body = nullptr;

			body = istream_null_new(*request.pool);
		} else if (request.body != nullptr)
			body = was_input_enable(*request.body);

		handler.OnWasRequest(*request.pool, request.method,
				     request.uri, std::move(*request.headers),
				     std::move(body));
		/* XXX check if connection has been closed */
	}

	return true;
}

void
WasServer::OnWasControlDone() noexcept
{
	assert(control.IsDefined());
}

void
WasServer::OnWasControlError(std::exception_ptr ep) noexcept
{
	assert(control.IsDefined());

	AbortError(ep);
}

void
WasServer::SendResponse(HttpStatus status,
			StringMap &&headers, UnusedIstreamPtr body) noexcept
{
	assert(request.state == Request::State::SUBMITTED);
	assert(response.body == nullptr);
	assert(http_status_is_valid(status));
	assert(!http_status_is_empty(status) || !body);

	if (!control.SendT(WAS_COMMAND_STATUS, status))
		return;

	if (body && http_method_is_empty(request.method)) {
		if (request.method == HttpMethod::HEAD) {
			off_t available = body.GetAvailable(false);
			if (available >= 0)
				headers.Add(AllocatorPtr{request.pool},
					    content_length_header,
					    FmtToBuffer(response.content_length_buffer,
							 "{}", available));
		}

		body.Clear();
	}

	Was::SendMap(control, WAS_COMMAND_HEADER, headers);

	if (body) {
		response.body = was_output_new(*request.pool,
					       control.GetEventLoop(),
					       socket.output, std::move(body),
					       *this);
		if (!control.Send(WAS_COMMAND_DATA) ||
		    !was_output_check_length(*response.body))
			return;
	} else {
		if (!control.Send(WAS_COMMAND_NO_DATA))
			return;
	}
}
