// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Server.hxx"
#include "Util.hxx"
#include "Error.hxx"
#include "SocketUtil.hxx"
#include "IstreamDataSource.hxx"
#include "Option.hxx"
#include "Callbacks.hxx"
#include "pool/pool.hxx"
#include "pool/PSocketAddress.hxx"
#include "http/server/Handler.hxx"
#include "http/CommonHeaders.hxx"
#include "http/Date.hxx"
#include "http/IncomingRequest.hxx"
#include "http/HeaderLimits.hxx"
#include "http/Headers.hxx"
#include "http/Logger.hxx"
#include "http/Method.hxx"
#include "http/Status.hxx"
#include "http/WaitTracker.hxx"
#include "istream/LengthIstream.hxx"
#include "istream/MultiFifoBufferIstream.hxx"
#include "istream/New.hxx"
#include "fs/FilteredSocket.hxx"
#include "lib/fmt/ToBuffer.hxx"
#include "event/Loop.hxx"
#include "net/StaticSocketAddress.hxx"
#include "net/log/ContentType.hxx"
#include "util/Cancellable.hxx"
#include "util/StaticVector.hxx"
#include "util/StringAPI.hxx"
#include "address_string.hxx"
#include "stopwatch.hxx"
#include "product.h" // for BRIEF_PRODUCT_TOKEN

#include <nghttp2/nghttp2.h>

#include <fmt/format.h>

#include <assert.h>

using std::string_view_literals::operator""sv;

namespace NgHttp2 {

static constexpr Event::Duration write_timeout = std::chrono::seconds(30);

static constexpr std::size_t FRAME_HEADER_SIZE = 9;

class ServerConnection::Request final
	: public IncomingHttpRequest, MultiFifoBufferIstreamHandler,
	  IstreamDataSourceHandler,
	  public IntrusiveListHook<>
{
	ServerConnection &connection;

	CancellablePointer cancel_ptr;

	MultiFifoBufferIstream *request_body_control = nullptr;

	WaitTracker wait_tracker;

	static constexpr WaitTracker::mask_t WAIT_RECEIVE_REQUEST = 1 << 0;
	static constexpr WaitTracker::mask_t WAIT_SEND_RESPONSE = 1 << 1;

	/**
	 * The response body if #error_status is set.
	 */
	const char *error_message;

	std::unique_ptr<IstreamDataSource> response_body;

	RootStopwatchPtr stopwatch;

	const uint32_t id;

	/**
	 * The response body status.  This is set by SendResponse(),
	 * and is used later for the access logger.
	 */
	HttpStatus the_status{};

	/**
	 * If this is set, the this library rejects the request with
	 * this HTTP status instead of letting the caller handle it.
	 * The field #error_message specifies the response body.
	 */
	HttpStatus error_status{};

	Net::Log::ContentType content_type{};

	/**
	 * This is set to true after at least one byte of the request
	 * body has been consumed.
	 */
	bool request_body_used = false;

public:
	uint_least64_t traffic_received = 0, traffic_sent = 0;

	explicit Request(PoolPtr &&_pool,
			 ServerConnection &_connection, uint32_t _id) noexcept
		:IncomingHttpRequest(std::move(_pool),
				     _connection.local_address,
				     _connection.remote_address,
				     _connection.local_host_and_port,
				     _connection.remote_host),
		 connection(_connection), id(_id)
	{
	}

	~Request() noexcept {
		/* clear stream_user_data to ignore future callbacks
		   on this stream */
		nghttp2_session_set_stream_user_data(connection.session.get(),
						     id, nullptr);

		if (request_body_control) {
			request_body_control->DestroyError(std::make_exception_ptr(std::runtime_error("Canceled")));
		}

		if (cancel_ptr)
			cancel_ptr.Cancel();

		if (logger != nullptr && (method != HttpMethod{} || uri != nullptr)) {
			int64_t length = -1;
			if (response_body)
				length = response_body->GetTransmitted();

			logger->LogHttpRequest(*this,
					       wait_tracker.GetDuration(GetEventLoop()),
					       the_status, content_type, length,
					       traffic_received, traffic_sent);
		}

		connection.RemoveRequest(*this);
	}

	void Destroy() noexcept {
		pool_trash(pool);
		this->~Request();
	}

	[[gnu::const]]
	EventLoop &GetEventLoop() noexcept {
		return connection.GetEventLoop();
	}

	nghttp2_data_provider MakeResponseDataProvider(UnusedIstreamPtr &&istream) noexcept {
		assert(!response_body);
		assert(istream);

		IstreamDataSourceHandler &h = *this;
		response_body = std::make_unique<IstreamDataSource>(std::move(istream),
								    h);
		return response_body->MakeDataProvider();
	}

	int OnReceiveRequest(bool has_request_body) noexcept;

	int OnEndDataFrame() noexcept;

	int OnStreamCloseCallback(uint32_t error_code) noexcept {
		if (request_body_control) {
			request_body_control->DestroyError(std::make_exception_ptr(MakeError(error_code, "Stream closed")));
			request_body_control = nullptr;
			wait_tracker.Clear(GetEventLoop(), WAIT_RECEIVE_REQUEST);
		}

		Destroy();
		return 0;
	}

	static int OnStreamCloseCallback(nghttp2_session *session,
					 int32_t stream_id,
					 uint32_t error_code, void *) noexcept {
		auto *request = (Request *)
			nghttp2_session_get_stream_user_data(session, stream_id);
		if (request == nullptr)
			return 0;

		return request->OnStreamCloseCallback(error_code);
	}

	int OnHeaderCallback(std::string_view name, std::string_view value) noexcept;

	static int OnHeaderCallback(nghttp2_session *session,
				    const nghttp2_frame *frame,
				    const uint8_t *name, size_t namelen,
				    const uint8_t *value, size_t valuelen,
				    uint8_t, void *) noexcept {
		if (frame->hd.type != NGHTTP2_HEADERS ||
		    frame->headers.cat != NGHTTP2_HCAT_REQUEST)
			return 0;

		auto *request = (Request *)
			nghttp2_session_get_stream_user_data(session, frame->hd.stream_id);
		if (request == nullptr)
			return 0;

		return request->OnHeaderCallback({(const char *)name, namelen},
						 {(const char *)value, valuelen});
	}

	int OnDataChunkReceivedCallback(std::span<const std::byte> data) noexcept;

	static int OnDataChunkReceivedCallback(nghttp2_session *session,
					       [[maybe_unused]] uint8_t flags,
					       int32_t stream_id,
					       const uint8_t *data,
					       size_t len,
					       [[maybe_unused]] void *user_data) noexcept {
		auto &c = *(ServerConnection *)user_data;

		/* always update the connection-level window to keep
		   it open for more data on other streams */
		c.Consume(len);

		auto *request = (Request *)
			nghttp2_session_get_stream_user_data(session, stream_id);
		if (request == nullptr)
			return 0;

		return request->OnDataChunkReceivedCallback(std::as_bytes(std::span{data, len}));
	}

	int OnFrameRecvCallback(const nghttp2_frame &frame) noexcept;
	int OnFrameSendCallback(const nghttp2_frame &frame) noexcept;

private:
	void SetError(HttpStatus _status, const char *_msg) noexcept {
		if (error_status != HttpStatus::UNDEFINED)
			/* use only the first error */
			return;

		error_status = _status;
		error_message = _msg;
	}

	void DeferWrite() noexcept {
		connection.DeferWrite();
	}

	void Consume(size_t nbytes) noexcept {
		nghttp2_session_consume_stream(connection.session.get(),
					       id, nbytes);
		DeferWrite();
	}

	/* virtual methods from class MultiFifoBufferIstreamHandler */
	void OnFifoBufferIstreamConsumed(size_t nbytes) noexcept override;
	void OnFifoBufferIstreamClosed() noexcept override;

	/* virtual methods from class IstreamDataSourceHandler */
	void OnIstreamDataSourceWaiting() noexcept override;
	void OnIstreamDataSourceReady() noexcept override;

	/* virtual methods from class IncomingHttpRequest */
	void SendResponse(HttpStatus status,
			  HttpHeaders &&response_headers,
			  UnusedIstreamPtr response_body) noexcept override;
};

[[gnu::pure]]
static HttpMethod
ParseHttpMethod(std::string_view s) noexcept
{
	for (size_t i = 0; i < size_t(HttpMethod::INVALID); ++i) {
		const char *name = http_method_to_string_data[i];
		if (name != nullptr && s == name)
			return static_cast<HttpMethod>(i);
	}

	return {};
}

inline int
ServerConnection::Request::OnHeaderCallback(std::string_view name,
					    std::string_view value) noexcept
{
	AllocatorPtr alloc(pool);

	if (name == ":method"sv) {
		method = ParseHttpMethod(value);
		if (method == HttpMethod{})
			SetError(HttpStatus::BAD_REQUEST,
				 "Unsupported request method\n");
	} else if (name == ":path"sv) {
		if (value.size() >= MAX_HTTP_HEADER_SIZE) {
			SetError(HttpStatus::REQUEST_URI_TOO_LONG,
				 "Request URI is too long\n");
			return 0;
		}

		uri = alloc.DupZ(value);
	} else if (name == ":authority"sv) {
		if (value.size() >= 1024) {
			SetError(HttpStatus::REQUEST_HEADER_FIELDS_TOO_LARGE,
				 "Host header is too long\n");
			return 0;
		}

		headers.Add(alloc, host_header, alloc.DupZ(value));
	} else if (name.size() >= 2 && name.front() != ':') {
		if (value.size() >= 8192) {
			SetError(HttpStatus::REQUEST_HEADER_FIELDS_TOO_LARGE,
				 "Request header is too long\n");
			return 0;
		}

		const char *allocated_name = alloc.DupZ(name);
		const char *allocated_value;

		/* the Cookie request header is special: multiple
		   headers are not concatenated with comma (RFC 2616
		   4.2), but with semicolon (RFC 6265 4.2.1); to avoid
		   confusion, it would be best to not concatenate
		   them, but leave them as separate headers, but when
		   proxying to Apache, Apache will conatenate them
		   unconditionally with comma via
		   apr_table_compress(APR_OVERLAP_TABLES_MERGE), which
		   breaks PHP's session management; as a workaround,
		   we concatenate all Cookie headers with a semicolon
		   here before Apache does the wrong thing */
		if (StringIsEqual(allocated_name, "cookie")) {
			const char *old_value = headers.Remove(cookie_header);
			if (old_value != nullptr)
				allocated_value = alloc.Concat(old_value, "; ",
							       value);
			else
				allocated_value = alloc.DupZ(value);
		} else
			allocated_value = alloc.DupZ(value);

		headers.Add(alloc, allocated_name, allocated_value);
	}

	return 0;
}

void
ServerConnection::Request::OnFifoBufferIstreamConsumed(size_t nbytes) noexcept
{
	if (!request_body_used) {
		request_body_used = true;

		/* now that the first byte has been consumed, and the
		   request body is really being used, revert to the
		   default window size */
		nghttp2_session_set_local_window_size(connection.session.get(),
						      NGHTTP2_NV_FLAG_NONE,
						      id, NGHTTP2_INITIAL_WINDOW_SIZE);
	}

	Consume(nbytes);

	wait_tracker.Set(GetEventLoop(), WAIT_RECEIVE_REQUEST);
}

void
ServerConnection::Request::OnFifoBufferIstreamClosed() noexcept
{
	assert(request_body_control);

	request_body_control = nullptr;

	wait_tracker.Clear(GetEventLoop(), WAIT_RECEIVE_REQUEST);
}

void
ServerConnection::Request::OnIstreamDataSourceWaiting() noexcept
{
	assert(response_body);
	assert(connection.socket);

	wait_tracker.Clear(GetEventLoop(), WAIT_SEND_RESPONSE);
}

void
ServerConnection::Request::OnIstreamDataSourceReady() noexcept
{
	assert(response_body);
	assert(connection.socket);

	wait_tracker.Set(GetEventLoop(), WAIT_SEND_RESPONSE);

	nghttp2_session_resume_data(connection.session.get(), id);
	DeferWrite();
}

inline int
ServerConnection::Request::OnDataChunkReceivedCallback(std::span<const std::byte> data) noexcept
{
	// TODO: limit the MultiFifoBuffer size

	if (request_body_control) {
		wait_tracker.Clear(GetEventLoop(), WAIT_RECEIVE_REQUEST);

		request_body_control->Push(data);
		request_body_control->SubmitBuffer();
	}

	return 0;
}

inline int
ServerConnection::Request::OnFrameRecvCallback(const nghttp2_frame &frame) noexcept
{
	traffic_received += FRAME_HEADER_SIZE + frame.hd.length;

	switch (frame.hd.type) {
	case NGHTTP2_HEADERS:
		if (frame.hd.flags & NGHTTP2_FLAG_END_HEADERS)
			return OnReceiveRequest((frame.hd.flags & NGHTTP2_FLAG_END_STREAM) == 0);

		break;

	case NGHTTP2_DATA:
		if (frame.hd.flags & NGHTTP2_FLAG_END_STREAM)
			return OnEndDataFrame();

		break;

	default:
		break;
	}

	return 0;
}

inline int
ServerConnection::Request::OnFrameSendCallback(const nghttp2_frame &frame) noexcept
{
	traffic_sent += FRAME_HEADER_SIZE + frame.hd.length;

	return 0;
}

inline int
ServerConnection::Request::OnReceiveRequest(bool has_request_body) noexcept
{
	if (error_status != HttpStatus{}) {
		SendMessage(error_status, error_message);
		return 0;
	}

	if (method == HttpMethod{} || uri == nullptr) {
		/* no method and no URI - refuse to handle this
		   request */
		nghttp2_submit_rst_stream(connection.session.get(),
					  NGHTTP2_FLAG_NONE,
					  id, NGHTTP2_CANCEL);
		DeferWrite();
		Destroy();
		return 0;
	}

	connection.handler.RequestHeadersFinished(*this);

	if (has_request_body) {
		MultiFifoBufferIstreamHandler &fbi_handler = *this;
		request_body_control = NewFromPool<MultiFifoBufferIstream>(pool, pool, fbi_handler);
		body = UnusedIstreamPtr(request_body_control);

		const char *content_length = headers.Remove(content_length_header);
		if (content_length != nullptr) {
			char *endptr;
			auto length = strtoul(content_length, &endptr, 10);
			if (endptr > content_length)
				body = NewIstreamPtr<LengthIstream>(pool,
								    std::move(body),
								    length);
		}

		wait_tracker.Set(GetEventLoop(), WAIT_RECEIVE_REQUEST);
	}

	stopwatch = RootStopwatchPtr(uri);

	connection.request_handler.HandleHttpRequest(*this,
						     stopwatch,
						     cancel_ptr);

	return 0;
}

inline int
ServerConnection::Request::OnEndDataFrame() noexcept
{
	if (!request_body_control)
		return 0;

	std::exchange(request_body_control, nullptr)->SetEof();
	wait_tracker.Clear(GetEventLoop(), WAIT_RECEIVE_REQUEST);
	return 0;
}

void
ServerConnection::Request::SendResponse(HttpStatus status,
					HttpHeaders &&response_headers,
					UnusedIstreamPtr _response_body) noexcept
{
	cancel_ptr = nullptr;

	the_status = status;

	StaticVector<nghttp2_nv, 256> hdrs;

	const fmt::format_int status_string{static_cast<unsigned>(status)};
	hdrs.push_back(MakeNv(":status", status_string.c_str()));

	if (response_headers.generate_date_header)
		/* RFC 2616 14.18: Date */
		hdrs.push_back(MakeNv("date", http_date_format(connection.socket->GetEventLoop().SystemNow())));

	if (response_headers.generate_server_header)
		/* RFC 2616 3.8: Product Tokens */
		hdrs.push_back(MakeNv("server", BRIEF_PRODUCT_TOKEN));

	if (generate_hsts_header)
		/* TODO: hard-coded to 90 days (7776000 seconds), but
		   this should probably be configurable */
		hdrs.push_back(MakeNv("strict-transport-security", "max-age=7776000"));

	StringBuffer<32> content_length_buffer;
	if (_response_body) {
		const auto content_length = _response_body.GetAvailable(false);
		if (content_length >= 0) {
			/* can't use fmt::format_int because it
			   doesn't have a default constructor */
			FmtToBuffer<32>(content_length_buffer, "{}", content_length);
			hdrs.push_back(MakeNv("content-length",
					      content_length_buffer.c_str()));
		}

		if (http_method_is_empty(method))
			_response_body.Clear();
	}

	const auto response_header_map = std::move(response_headers).ToMap(AllocatorPtr{pool});
	if (const char *ct = response_header_map.Get(content_type_header))
		content_type = Net::Log::ParseContentType(ct);

	for (const auto &i : response_header_map) {
		if (hdrs.full())
			// TODO: what now?
			break;

		hdrs.push_back(MakeNv(i.key, i.value));
	}

	nghttp2_data_provider dp, *dpp = nullptr;
	if (_response_body) {
		dp = MakeResponseDataProvider(std::move(_response_body));
		dpp = &dp;
	}

	nghttp2_submit_response(connection.session.get(), id,
				hdrs.data(), hdrs.size(),
				dpp);
	DeferWrite();
}

ServerConnection::ServerConnection(struct pool &_pool,
				   UniquePoolPtr<FilteredSocket> _socket,
				   SocketAddress _remote_address,
				   HttpServerConnectionHandler &_handler,
				   HttpServerRequestHandler &_request_handler)
	:pool(_pool), socket(std::move(_socket)),
	 handler(_handler), request_handler(_request_handler),
	 local_address(DupAddress(pool, socket->GetSocket().GetLocalAddress())),
	 remote_address(DupAddress(pool, _remote_address)),
	 local_host_and_port(address_to_string(pool, local_address)),
	 remote_host(address_to_host_string(pool, remote_address)),
	 idle_timer(socket->GetEventLoop(), BIND_THIS_METHOD(OnIdleTimeout))
{
	socket->Reinit(write_timeout, *this);

	NgHttp2::Option option;
	//nghttp2_option_set_recv_client_preface(option.get(), 1);
	nghttp2_option_set_no_auto_window_update(option.get(), true);

	NgHttp2::SessionCallbacks callbacks;
	nghttp2_session_callbacks_set_send_callback(callbacks.get(), SendCallback);
	nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks.get(),
							     OnFrameRecvCallback);
	nghttp2_session_callbacks_set_on_frame_send_callback(callbacks.get(),
							     OnFrameSendCallback);
	nghttp2_session_callbacks_set_on_stream_close_callback(callbacks.get(),
							       Request::OnStreamCloseCallback);
	nghttp2_session_callbacks_set_on_header_callback(callbacks.get(),
							 Request::OnHeaderCallback);
	nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks.get(),
								  Request::OnDataChunkReceivedCallback);
	nghttp2_session_callbacks_set_on_begin_headers_callback(callbacks.get(),
								OnBeginHeaderCallback);
	nghttp2_session_callbacks_set_on_invalid_frame_recv_callback(callbacks.get(),
								     OnInvalidFrameReceivedCallback);

	session = NgHttp2::Session::NewServer(callbacks.get(), this, option.get());

	static constexpr nghttp2_settings_entry iv[] = {
		{NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 64},

		/* until a request body is really being used, allow
		   the client to upload only the first 4 kB to avoid
		   congesting the connection-level window; this will
		   be reverted to the 64 kB default later by
		   Request::OnFifoBufferIstreamConsumed() */
		{NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, 4096},
	};

	const auto rv = nghttp2_submit_settings(session.get(), NGHTTP2_FLAG_NONE,
						iv, std::size(iv));
	if (rv != 0)
		throw MakeError(rv, "nghttp2_submit_settings() failed");

	/* allow the connection-level window size to be somewhat
	   larger than the default 64 kB for better concurrent upload
	   performance */
	nghttp2_session_set_local_window_size(session.get(),
					      NGHTTP2_NV_FLAG_NONE,
					      0, 256 * 1024);

	idle_timer.Schedule(idle_timeout);

	DeferWrite();
	socket->ScheduleRead();
}

ServerConnection::~ServerConnection() noexcept
{
	while (!requests.empty())
		requests.front().Destroy();
}

inline void
ServerConnection::OnIdleTimeout() noexcept
{
	// TODO send HTTP/2 GOAWAY and TLS close alert?

	handler.HttpConnectionClosed();
}

inline void
ServerConnection::RemoveRequest(Request &request) noexcept
{
	assert(!requests.empty());
	assert(!idle_timer.IsPending());

	request.unlink();

	if (requests.empty())
		idle_timer.Schedule(idle_timeout);
}

inline void
ServerConnection::DeferWrite() noexcept
{
	socket->DeferWrite();
}

ssize_t
ServerConnection::SendCallback(std::span<const std::byte> src) noexcept
{
	return SendToBuffer(*socket, src);
}

int
ServerConnection::OnFrameRecvCallback(const nghttp2_frame &frame) noexcept
{
	if (frame.hd.stream_id != 0) {
		Request *request = static_cast<Request *>(nghttp2_session_get_stream_user_data(session.get(),
											       frame.hd.stream_id));
		if (request != nullptr)
			return request->OnFrameRecvCallback(frame);
	}

	return 0;
}

int
ServerConnection::OnFrameSendCallback(const nghttp2_frame &frame) noexcept
{
	if (frame.hd.stream_id != 0) {
		Request *request = static_cast<Request *>(nghttp2_session_get_stream_user_data(session.get(),
											       frame.hd.stream_id));
		if (request != nullptr)
			return request->OnFrameSendCallback(frame);
	}

	return 0;
}

int
ServerConnection::OnBeginHeaderCallback(const nghttp2_frame &frame) noexcept
{
	if (frame.hd.type == NGHTTP2_HEADERS &&
	    frame.headers.cat == NGHTTP2_HCAT_REQUEST) {

		auto stream_pool = pool_new_linear(&pool,
						   "NgHttp2ServerRequest", 8192);
		pool_set_major(stream_pool);

		assert(requests.empty() == idle_timer.IsPending());
		idle_timer.Cancel();

		auto *request = NewFromPool<Request>(std::move(stream_pool),
						     *this, frame.hd.stream_id);
		request->traffic_received += FRAME_HEADER_SIZE + frame.hd.length;
		requests.push_front(*request);
		nghttp2_session_set_stream_user_data(session.get(),
						     frame.hd.stream_id,
						     request);
		return 0;
	} else
		return 0;
}

int
ServerConnection::OnInvalidFrameReceivedCallback([[maybe_unused]] const nghttp2_frame &frame,
						 [[maybe_unused]] int lib_error_code) noexcept
{
	handler.OnInvalidFrameReceived();
	return 0;
}

BufferedResult
ServerConnection::OnBufferedData()
{
	return ReceiveFromSocketBuffer(session.get(), *socket);
}

bool
ServerConnection::OnBufferedClosed() noexcept
{
	// TODO
	handler.HttpConnectionClosed();
	return false;
}

bool
ServerConnection::OnBufferedWrite()
{
	return OnSocketWrite(session.get(), *socket);
}

void
ServerConnection::OnBufferedError(std::exception_ptr e) noexcept
{
	handler.HttpConnectionError(std::move(e));
}

} // namespace NgHttp2
