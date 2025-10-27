// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Internal.hxx"
#include "Handler.hxx"
#include "Request.hxx"
#include "http/Logger.hxx"
#include "pool/pool.hxx"
#include "pool/PSocketAddress.hxx"
#include "istream/Bucket.hxx"
#include "system/Error.hxx"
#include "net/PToString.hxx"
#include "net/TimeoutError.hxx"
#include "io/Iovec.hxx"
#include "util/StaticVector.hxx"

#include <stdexcept>

#include <assert.h>
#include <unistd.h>

using std::string_view_literals::operator""sv;

void
HttpServerConnection::Log(HttpServerRequest &r) noexcept
{
	auto *logger = r.logger;
	if (logger == nullptr)
		return;

	logger->LogHttpRequest(r,
			       wait_tracker.GetDuration(GetEventLoop()),
			       response.status,
			       response.content_type,
			       response.status != HttpStatus{} ? response.length : -1,
			       request.bytes_received,
			       response.bytes_sent);
}

HttpServerRequest *
HttpServerConnection::NewRequest(HttpMethod method,
				 std::string_view uri) noexcept
{
	response.status = {};

	auto request_pool = pool_new_slice(*pool, "HttpServerRequest", request_slice_pool);
	pool_set_major(request_pool);

	return NewFromPool<HttpServerRequest>(std::move(request_pool),
					      *this,
					      local_address,
					      remote_address,
					      local_host_and_port,
					      remote_host,
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
	assert(!request.cancel_ptr);

	if (socket->HasFilter())
		return BucketResult::FALLBACK;

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
			? (list.ShouldFallback()
			   ? BucketResult::FALLBACK
			   : BucketResult::LATER)
			: BucketResult::DEPLETED;
	}

	ssize_t nbytes = v.size() == 1
		? socket->Write(ToSpan(v.front()))
		: socket->WriteV(v);
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

	return r.eof
		? BucketResult::DEPLETED
		: (list.ShouldFallback()
		   ? BucketResult::FALLBACK
		   : BucketResult::MORE);
}

HttpServerConnection::BucketResult
HttpServerConnection::TryWriteBuckets() noexcept
{
	BucketResult result;

	try {
		result = TryWriteBuckets2();
	} catch (...) {
		assert(!HasInput());

		Error(std::current_exception());
		return BucketResult::DESTROYED;
	}

	switch (result) {
	case BucketResult::FALLBACK:
	case BucketResult::LATER:
		assert(HasInput());
		break;

	case BucketResult::MORE:
	case BucketResult::BLOCKING:
		assert(HasInput());
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

inline bool
HttpServerConnection::TryWrite() noexcept
{
	assert(IsValid());
	assert(request.read_state != Request::START &&
	       request.read_state != Request::HEADERS);
	assert(request.request != nullptr);
	assert(HasInput());

	switch (TryWriteBuckets()) {
	case BucketResult::FALLBACK:
		break;

	case BucketResult::LATER:
	case BucketResult::MORE:
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

	if (!response.want_write) {
		socket->UnscheduleWrite();
		wait_tracker.Clear(GetEventLoop(), WAIT_SEND_RESPONSE);
	}

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
	switch (request.read_state) {
	case Request::START:
		break;

	case Request::HEADERS:
		request.read_state = Request::END;
		keep_alive = false;
		request.request->SendMessage(HttpStatus::REQUEST_TIMEOUT,
					     "Request header timeout"sv);
		return;

	case Request::BODY:
		if (!HasInput()) {
			assert(request.cancel_ptr);

			/* this cancellation disables keep-alive */
			request.cancel_ptr.Cancel();

			request.request->SendMessage(HttpStatus::REQUEST_TIMEOUT,
						     "Request body timeout"sv);
			return;
		}

		break;

	case Request::END:
		assert(false);
	}

	SocketError(TimeoutError{});
}

inline
HttpServerConnection::HttpServerConnection(struct pool &_pool,
					   UniquePoolPtr<FilteredSocket> &&_socket,
					   SocketAddress _local_address,
					   SocketAddress _remote_address,
					   bool _date_header,
					   SlicePool &_request_slice_pool,
					   HttpServerConnectionHandler &_handler,
					   HttpServerRequestHandler &_request_handler) noexcept
	:pool(&_pool), request_slice_pool(_request_slice_pool),
	 socket(std::move(_socket)),
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

#ifdef HAVE_URING
	if (auto *uring_queue = socket->GetUringQueue()) {
		uring_splice.emplace(*this, *uring_queue);
	}
#endif

	idle_timer.Schedule(idle_timeout);

	/* read the first request, but not in this stack frame, because a
	   failure may destroy the HttpServerConnection before it gets
	   passed to the caller */
	if (!socket->HasUring())
		socket->DeferRead();
}

inline
HttpServerConnection::~HttpServerConnection() noexcept
{
#ifdef HAVE_URING
	CancelUringSend();
#endif
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
			   SlicePool &slice_pool,
			   HttpServerConnectionHandler &handler,
			   HttpServerRequestHandler &request_handler) noexcept
{
	assert(socket);

	return NewFromPool<HttpServerConnection>(pool, pool,
						 std::move(socket),
						 local_address, remote_address,
						 date_header,
						 slice_pool,
						 handler, request_handler);
}

void
HttpServerConnection::CloseRequest() noexcept
{
	assert(request.read_state != Request::START);
	assert(request.request != nullptr);

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

	Log(*_request);

	_request->Destroy();

	/* the handler must have closed the request body */
	assert(request.read_state != Request::BODY);
}

void
HttpServerConnection::Done() noexcept
{
	assert(handler != nullptr);
	assert(request.read_state == Request::START);

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
