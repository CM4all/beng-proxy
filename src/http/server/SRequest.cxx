// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Internal.hxx"
#include "Request.hxx"

BufferedResult
HttpServerConnection::FeedRequestBody(std::span<const std::byte> src) noexcept
{
	assert(request.read_state == Request::BODY);
	assert(request.body_state == Request::BodyState::READING);
	assert(!response.pending_drained);

	const DestructObserver destructed(*this);

	std::size_t nbytes = request_body_reader->FeedBody(src);
	if (nbytes == 0) {
		if (destructed)
			return BufferedResult::CLOSED;

		read_timer.Cancel();
		return BufferedResult::OK;
	}

	request.bytes_received += nbytes;
	socket->DisposeConsumed(nbytes);

	assert(request.read_state == Request::BODY);

	if (request_body_reader->IsEOF()) {
		request.read_state = Request::END;
#ifndef NDEBUG
		request.body_state = Request::BodyState::CLOSED;
#endif

		read_timer.Cancel();

		if (socket->IsConnected())
			socket->SetDirect(false);

		request.request->stopwatch.RecordEvent("request_end");

		request_body_reader->DestroyEof();
		if (destructed)
			return BufferedResult::CLOSED;
	} else
		/* refresh the request body timeout */
		ScheduleReadTimeoutTimer();

	return BufferedResult::OK;
}

void
HttpServerConnection::DiscardRequestBody() noexcept
{
	assert(request.read_state == Request::BODY);
	assert(request.body_state == Request::BodyState::READING);
	assert(!response.pending_drained);

	if (!socket->IsValid() || !socket->IsConnected()) {
		/* this happens when there's an error on the socket while
		   reading the request body before the response gets
		   submitted, and this HTTP server library invokes the
		   handler's abort method; the handler will free the request
		   body, but the socket is already closed */
		assert(request.request == nullptr);
	}

	request.read_state = Request::END;
#ifndef NDEBUG
	request.body_state = Request::BodyState::CLOSED;
#endif

	read_timer.Cancel();

	if (socket->IsConnected())
		socket->SetDirect(false);

	if (request.expect_100_continue)
		/* the request body was optional, and we did not send the "100
		   Continue" response (yet): pretend there never was a request
		   body */
		request.expect_100_continue = false;
	else if (request_body_reader->Discard(*socket))
		/* the remaining data has already been received into the input
		   buffer, and we only need to discard it from there to have a
		   "clean" connection */
		return;
	else
		/* disable keep-alive so we don't need to wait for the client
		   to finish sending the request body */
		keep_alive = false;
}

off_t
HttpServerConnection::RequestBodyReader::_GetAvailable(bool partial) noexcept
{
	assert(connection.IsValid());
	assert(connection.request.read_state == Request::BODY);
	assert(connection.request.body_state == Request::BodyState::READING);
	assert(!connection.response.pending_drained);

	return HttpBodyReader::GetAvailable(*connection.socket, partial);
}

inline void
HttpServerConnection::ReadRequestBody() noexcept
{
	assert(IsValid());
	assert(request.read_state == Request::BODY);
	assert(request.body_state == Request::BodyState::READING);
	assert(!response.pending_drained);

	if (!MaybeSend100Continue())
		return;

	if (request.in_handler)
		/* avoid recursion */
		return;

	if (socket->IsConnected())
		socket->SetDirect(request_body_reader->CheckDirect(socket->GetType()));

	socket->Read();
}

void
HttpServerConnection::RequestBodyReader::_Read() noexcept
{
	connection.ReadRequestBody();
}

void
HttpServerConnection::RequestBodyReader::_ConsumeDirect(std::size_t nbytes) noexcept
{
	HttpBodyReader::_ConsumeDirect(nbytes);

	connection.request.bytes_received += nbytes;
}

void
HttpServerConnection::RequestBodyReader::_Close() noexcept
{
	if (connection.request.read_state == Request::END)
		return;

	if (connection.request.request != nullptr)
		connection.request.request->stopwatch.RecordEvent("close");

	connection.DiscardRequestBody();

	Destroy();
}
