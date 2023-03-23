// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Internal.hxx"
#include "Handler.hxx"
#include "Request.hxx"
#include "address_string.hxx"
#include "http/Logger.hxx"
#include "pool/pool.hxx"
#include "pool/PSocketAddress.hxx"
#include "istream/Bucket.hxx"
#include "system/Error.hxx"
#include "net/TimeoutError.hxx"
#include "io/Iovec.hxx"
#include "util/StaticVector.hxx"

#include <stdexcept>

#include <assert.h>
#include <unistd.h>

void
HttpServerConnection::Log() noexcept
{
	auto &r = *request.request;
	auto *logger = r.logger;
	if (logger == nullptr)
		return;

	logger->LogHttpRequest(r,
			       response.status,
			       response.status != HttpStatus{} ? response.length : -1,
			       request.bytes_received,
			       response.bytes_sent);
}

HttpServerRequest *
http_server_request_new(HttpServerConnection *connection,
			HttpMethod method,
			std::string_view uri) noexcept
{
	assert(connection != nullptr);

	connection->response.status = {};

	auto pool = pool_new_linear(connection->pool,
				    "http_server_request", 8192);
	pool_set_major(pool);

	return NewFromPool<HttpServerRequest>(std::move(pool),
					      *connection,
					      connection->local_address,
					      connection->remote_address,
					      connection->local_host_and_port,
					      connection->remote_host,
					      method, uri);
}

HttpServerConnection::BucketResult
HttpServerConnection::TryWriteBuckets2()
{
	assert(IsValid());
	assert(request.read_state != Request::START &&
	       request.read_state != Request::HEADERS);
	assert(request.request != nullptr);
	assert(HasInput());

	if (socket->HasFilter())
		return BucketResult::UNAVAILABLE;

	IstreamBucketList list;

	try {
		input.FillBucketList(list);
	} catch (...) {
		std::throw_with_nested(std::runtime_error("error on HTTP response stream"));
	}

	StaticVector<struct iovec, 64> v;
	for (const auto &bucket : list) {
		if (!bucket.IsBuffer())
			break;

		v.push_back(MakeIovec(bucket.GetBuffer()));

		if (v.full())
			break;
	}

	if (v.empty()) {
		return list.HasMore()
			? BucketResult::UNAVAILABLE
			: BucketResult::DEPLETED;
	}

	ssize_t nbytes = socket->WriteV(v);
	if (nbytes < 0) {
		if (nbytes == WRITE_BLOCKING) [[likely]]
			return BucketResult::BLOCKING;

		if (nbytes == WRITE_DESTROYED)
			return BucketResult::DESTROYED;

		SocketErrorErrno("write error on HTTP connection");
		return BucketResult::DESTROYED;
	}

	response.bytes_sent += nbytes;
	response.length += nbytes;

	const auto r = input.ConsumeBucketList(nbytes);
	assert(r.consumed == (std::size_t)nbytes);

	// TODO remove the IsDepleted() check
	return r.eof || list.IsDepleted(r.consumed)
		? BucketResult::DEPLETED
		: BucketResult::MORE;
}

HttpServerConnection::BucketResult
HttpServerConnection::TryWriteBuckets() noexcept
{
	BucketResult result;

	try {
		result = TryWriteBuckets2();
	} catch (...) {
		assert(!HasInput());

		/* we clear this CancellablePointer here so CloseRequest()
		   won't think we havn't sent a response yet */
		request.cancel_ptr = nullptr;

		Error(std::current_exception());
		return BucketResult::DESTROYED;
	}

	switch (result) {
	case BucketResult::UNAVAILABLE:
	case BucketResult::MORE:
		assert(HasInput());
		break;

	case BucketResult::BLOCKING:
		assert(HasInput());
		response.want_write = true;
		ScheduleWrite();
		break;

	case BucketResult::DEPLETED:
		assert(HasInput());
		CloseInput();
		if (!ResponseIstreamFinished())
			result = BucketResult::DESTROYED;
		break;

	case BucketResult::DESTROYED:
		break;
	}

	return result;
}

bool
HttpServerConnection::TryWrite() noexcept
{
	assert(IsValid());
	assert(request.read_state != Request::START &&
	       request.read_state != Request::HEADERS);
	assert(request.request != nullptr);
	assert(HasInput());

	switch (TryWriteBuckets()) {
	case BucketResult::UNAVAILABLE:
	case BucketResult::MORE:
		break;

	case BucketResult::BLOCKING:
	case BucketResult::DEPLETED:
		return true;

	case BucketResult::DESTROYED:
		return false;
	}

	const DestructObserver destructed(*this);
	input.Read();
	return !destructed;
}

/*
 * buffered_socket handler
 *
 */

BufferedResult
HttpServerConnection::OnBufferedData()
{
	auto r = socket->ReadBuffer();
	assert(!r.empty());

	if (response.pending_drained) {
		/* discard all incoming data while we're waiting for the
		   (filtered) response to be drained */
		socket->DisposeConsumed(r.size());
		return BufferedResult::OK;
	}

	return Feed(r);
}

DirectResult
HttpServerConnection::OnBufferedDirect(SocketDescriptor fd, FdType fd_type)
{
	assert(request.read_state != Request::END);
	assert(!response.pending_drained);

	return TryRequestBodyDirect(fd, fd_type);
}

bool
HttpServerConnection::OnBufferedWrite()
{
	assert(!response.pending_drained);

	response.want_write = false;

	if (!TryWrite())
		return false;

	if (!response.want_write)
		socket->UnscheduleWrite();

	return true;
}

bool
HttpServerConnection::OnBufferedDrained() noexcept
{
	if (response.pending_drained) {
		Done();
		return false;
	}

	return true;
}

bool
HttpServerConnection::OnBufferedHangup() noexcept
{
	Cancel();
	return false;
}

bool
HttpServerConnection::OnBufferedClosed() noexcept
{
	Cancel();
	return false;
}

void
HttpServerConnection::OnBufferedError(std::exception_ptr ep) noexcept
{
	SocketError(ep);
}

inline void
HttpServerConnection::IdleTimeoutCallback() noexcept
{
	assert(request.read_state == Request::START ||
	       request.read_state == Request::HEADERS);

	Cancel();
}

inline void
HttpServerConnection::OnReadTimeout() noexcept
{
	assert(request.read_state == Request::BODY);

	// TODO send "408 Request Timeout"?

	SocketError(TimeoutError{});
}

inline
HttpServerConnection::HttpServerConnection(struct pool &_pool,
					   UniquePoolPtr<FilteredSocket> &&_socket,
					   SocketAddress _local_address,
					   SocketAddress _remote_address,
					   bool _date_header,
					   HttpServerConnectionHandler &_handler,
					   HttpServerRequestHandler &_request_handler) noexcept
	:pool(&_pool), socket(std::move(_socket)),
	 idle_timer(socket->GetEventLoop(),
		    BIND_THIS_METHOD(IdleTimeoutCallback)),
	 read_timer(socket->GetEventLoop(),
		    BIND_THIS_METHOD(OnReadTimeout)),
	 handler(&_handler), request_handler(_request_handler),
	 local_address(DupAddress(*pool, _local_address)),
	 remote_address(DupAddress(*pool, _remote_address)),
	 local_host_and_port(address_to_string(*pool, _local_address)),
	 remote_host(address_to_host_string(*pool, _remote_address)),
	 date_header(_date_header)
{
	socket->Reinit(write_timeout, *this);

	idle_timer.Schedule(idle_timeout);

	/* read the first request, but not in this stack frame, because a
	   failure may destroy the HttpServerConnection before it gets
	   passed to the caller */
	socket->DeferRead();
}

void
HttpServerConnection::Delete() noexcept
{
	this->~HttpServerConnection();
}

HttpServerConnection *
http_server_connection_new(struct pool &pool,
			   UniquePoolPtr<FilteredSocket> socket,
			   SocketAddress local_address,
			   SocketAddress remote_address,
			   bool date_header,
			   HttpServerConnectionHandler &handler,
			   HttpServerRequestHandler &request_handler) noexcept
{
	assert(socket);

	return NewFromPool<HttpServerConnection>(pool, pool,
						 std::move(socket),
						 local_address, remote_address,
						 date_header,
						 handler, request_handler);
}

void
HttpServerConnection::CloseRequest() noexcept
{
	assert(request.read_state != Request::START);
	assert(request.request != nullptr);

	Log();

	auto *_request = std::exchange(request.request, nullptr);

	if ((request.read_state == Request::BODY ||
	     request.read_state == Request::END)) {
		if (HasInput())
			CloseInput();
		else if (request.cancel_ptr)
			/* don't call this if coming from
			   _response_stream_abort() */
			request.cancel_ptr.Cancel();
	}

	_request->Destroy();

	/* the handler must have closed the request body */
	assert(request.read_state != Request::BODY);
}

void
HttpServerConnection::Done() noexcept
{
	assert(handler != nullptr);
	assert(request.read_state == Request::START);

	/* shut down the socket gracefully to allow the TCP stack to
	   transfer remaining response data */
	socket->Shutdown();

	auto *_handler = handler;
	handler = nullptr;

	Delete();

	_handler->HttpConnectionClosed();
}

void
HttpServerConnection::Cancel() noexcept
{
	assert(handler != nullptr);

	if (request.request != nullptr)
		request.request->stopwatch.RecordEvent("cancel");

	if (request.read_state != Request::START)
		CloseRequest();

	auto *_handler = std::exchange(handler, nullptr);

	Delete();

	if (_handler != nullptr)
		_handler->HttpConnectionClosed();
}

void
HttpServerConnection::Error(std::exception_ptr e) noexcept
{
	assert(handler != nullptr);

	if (request.read_state != Request::START)
		CloseRequest();

	auto *_handler = std::exchange(handler, nullptr);

	Delete();

	if (_handler != nullptr)
		_handler->HttpConnectionError(e);
}

void
HttpServerConnection::Error(const char *msg) noexcept
{
	Error(std::make_exception_ptr(std::runtime_error(msg)));
}

void
http_server_connection_close(HttpServerConnection *connection) noexcept
{
	assert(connection != nullptr);

	connection->handler = nullptr;

	if (connection->request.read_state != HttpServerConnection::Request::START)
		connection->CloseRequest();

	connection->Delete();
}

void
HttpServerConnection::SocketErrorErrno(const char *msg) noexcept
{
	if (errno == EPIPE || errno == ECONNRESET) {
		/* don't report this common problem */
		Cancel();
		return;
	}

	try {
		throw MakeErrno(msg);
	} catch (...) {
		Error(std::make_exception_ptr(HttpServerSocketError()));
	}
}

void
http_server_connection_graceful(HttpServerConnection *connection) noexcept
{
	assert(connection != nullptr);

	if (connection->request.read_state == HttpServerConnection::Request::START)
		/* there is no request currently; close the connection
		   immediately */
		connection->Done();
	else
		/* a request is currently being handled; disable keep_alive so
		   the connection will be closed after this last request */
		connection->keep_alive = false;
}

enum http_server_score
http_server_connection_score(const HttpServerConnection *connection) noexcept
{
	return connection->score;
}
