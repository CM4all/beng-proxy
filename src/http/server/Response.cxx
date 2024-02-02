// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Internal.hxx"
#include "Request.hxx"
#include "Handler.hxx"
#include "io/SpliceSupport.hxx"
#include "util/Compiler.h"
#include "util/Exception.hxx"

#include <errno.h>
#include <string.h>

IstreamReadyResult
HttpServerConnection::OnIstreamReady() noexcept
{
	switch (TryWriteBuckets()) {
	case BucketResult::FALLBACK:
		return IstreamReadyResult::FALLBACK;

	case BucketResult::MORE:
		/* it's our responsibility now to ask for more data */
		socket->ScheduleWrite();
		return IstreamReadyResult::OK;

	case BucketResult::LATER:
	case BucketResult::BLOCKING:
		return IstreamReadyResult::OK;

	case BucketResult::DEPLETED:
	case BucketResult::DESTROYED:
		return IstreamReadyResult::CLOSED;
	}

	gcc_unreachable();
}

std::size_t
HttpServerConnection::OnData(std::span<const std::byte> src) noexcept
{
	assert(socket->IsConnected() || request.request == nullptr);
	assert(HasInput());
	assert(!response.pending_drained);

	if (!socket->IsConnected())
		return 0;

	ssize_t nbytes = socket->Write(src);

	if (nbytes >= 0) [[likely]] {
		response.bytes_sent += nbytes;
		response.length += (off_t)nbytes;
		ScheduleWrite();
		return (std::size_t)nbytes;
	}

	if (nbytes == WRITE_BLOCKING) [[likely]] {
		response.want_write = true;
		return 0;
	}

	if (nbytes == WRITE_DESTROYED)
		return 0;

	SocketErrorErrno("write error on HTTP connection");
	return 0;
}

IstreamDirectResult
HttpServerConnection::OnDirect(FdType type, FileDescriptor fd, off_t offset,
			       std::size_t max_length, bool then_eof) noexcept
{
	assert(socket->IsConnected() || request.request == nullptr);
	assert(HasInput());
	assert(!response.pending_drained);

	if (!socket->IsConnected())
		return IstreamDirectResult::BLOCKING;

	ssize_t nbytes = socket->WriteFrom(fd, type, ToOffsetPointer(offset),
					   max_length);
	if (nbytes > 0) [[likely]] {
		input.ConsumeDirect(nbytes);
		response.bytes_sent += nbytes;
		response.length += (off_t)nbytes;

		if (then_eof && static_cast<std::size_t>(nbytes) == max_length) {
			CloseInput();
			ResponseIstreamFinished();
			return IstreamDirectResult::CLOSED;
		}

		ScheduleWrite();
		return IstreamDirectResult::OK;
	} else if (nbytes == WRITE_BLOCKING) {
		response.want_write = true;
		return IstreamDirectResult::BLOCKING;
	} else if (nbytes == WRITE_DESTROYED)
		return IstreamDirectResult::CLOSED;
	else if (nbytes == WRITE_SOURCE_EOF)
		return IstreamDirectResult::END;
	else {
		if (errno == EAGAIN)
			socket->UnscheduleWrite();
		return IstreamDirectResult::ERRNO;
	}
}

void
HttpServerConnection::OnEof() noexcept
{
	assert(request.read_state != Request::START &&
	       request.read_state != Request::HEADERS);
	assert(request.request != nullptr);
	assert(HasInput());
	assert(!response.pending_drained);

	ClearInput();

	ResponseIstreamFinished();
}

void
HttpServerConnection::OnError(std::exception_ptr ep) noexcept
{
	assert(HasInput());

	ClearInput();

	/* we clear this cancel_ptr here so http_server_request_close()
	   won't think we havn't sent a response yet */
	request.cancel_ptr = nullptr;

	Error(NestException(ep,
			    std::runtime_error("error on HTTP response stream")));
}

void
HttpServerConnection::SetResponseIstream(UnusedIstreamPtr r)
{
	SetInput(std::move(r));
	input.SetDirect(istream_direct_mask_to(socket->GetType()));
}

bool
HttpServerConnection::ResponseIstreamFinished()
{
	socket->UnscheduleWrite();

	if (handler != nullptr)
		handler->ResponseFinished();

	Log(*request.request);

	/* check for end of chunked request body again, just in case
	   DechunkIstream has announced this in a derred event */
	if (request.read_state == Request::BODY && request_body_reader->IsEOF()) {
		request.read_state = Request::END;
#ifndef NDEBUG
		request.body_state = Request::BodyState::CLOSED;
#endif

		read_timer.Cancel();

		if (socket->IsConnected())
			socket->SetDirect(false);

		const DestructObserver destructed(*this);
		request_body_reader->DestroyEof();
		if (destructed)
			return false;
	}

	if (request.read_state == Request::BODY) {
		/* We are still reading the request body, which we don't need
		   anymore.  To discard it, we simply close the connection by
		   disabling keepalive; this seems cheaper than redirecting
		   the rest of the body to /dev/null */
		DiscardRequestBody();

		const DestructObserver destructed(*this);
		request_body_reader->DestroyError(std::make_exception_ptr(std::runtime_error("request body discarded")));
		if (destructed)
			return false;
	}

	assert(!read_timer.IsPending());

	request.request->stopwatch.RecordEvent("response_end");
	request.request->Destroy();
	request.request = nullptr;
	response.bytes_sent = 0;

	request.Reset();

	if (keep_alive) {
		/* handle pipelined request (if any), or set up events for
		   next request */

		idle_timer.Schedule(idle_timeout);

		return true;
	} else {
		/* keepalive disabled and response is finished: we must close
		   the connection */

		/* shut down the socket gracefully to allow the TCP
		   stack to transfer remaining response data */
		socket->Shutdown();

		if (socket->IsDrained()) {
			Done();
			return false;
		} else {
			/* there is still data in the filter's output buffer; wait for
			   that to drain, which will trigger
			   http_server_socket_drained() */
			assert(!response.pending_drained);

			response.pending_drained = true;

			return true;
		}
	}
}
