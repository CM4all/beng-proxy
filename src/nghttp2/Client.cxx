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

#include "Client.hxx"
#include "Util.hxx"
#include "IstreamDataSource.hxx"
#include "Option.hxx"
#include "Callbacks.hxx"
#include "pool/pool.hxx"
#include "istream/LengthIstream.hxx"
#include "istream/MultiFifoBufferIstream.hxx"
#include "istream/New.hxx"
#include "fs/FilteredSocket.hxx"
#include "util/Cancellable.hxx"
#include "util/RuntimeError.hxx"
#include "util/StaticArray.hxx"
#include "util/StringView.hxx"
#include "http/ResponseHandler.hxx"
#include "stopwatch.hxx"
#include "strmap.hxx"

#include <nghttp2/nghttp2.h>

#include <assert.h>

namespace NgHttp2 {

static constexpr Event::Duration write_timeout = std::chrono::seconds(30);

class ClientConnection::Request final
	: Cancellable, MultiFifoBufferIstreamHandler,
	  IstreamDataSourceHandler,
	  public boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>>
{
	struct pool &pool;

	enum class State {
		INITIAL,

		/**
		 * Receiving response headers.
		 */
		HEADERS,

		/**
		 * Receiving the response body.  The
		 * #HttpResponseHandler has been invoked already.
		 */
		BODY,
	} state = State::INITIAL;

	const StopwatchPtr stopwatch;

	ClientConnection &connection;

	HttpResponseHandler &handler;

	int32_t id = -1;

	http_status_t status = HTTP_STATUS_OK;

	bool eof = false;

	StringMap response_headers;

	MultiFifoBufferIstream *response_body_control;

	std::unique_ptr<IstreamDataSource> request_body;

public:
	explicit Request(struct pool &_pool,
			 StopwatchPtr &&_stopwatch,
			 ClientConnection &_connection,
			 HttpResponseHandler &_handler,
			 CancellablePointer &cancel_ptr) noexcept
		:pool(_pool),
		 stopwatch(std::move(_stopwatch)),
		 connection(_connection), handler(_handler)
	{
		cancel_ptr = *this;
	}

	~Request() noexcept {
		if (id >= 0) {
			if (response_body_control)
				Consume(response_body_control->GetAvailable());

			/* clear stream_user_data to ignore future
			   callbacks on this stream */
			nghttp2_session_set_stream_user_data(connection.session.get(),
							     id, nullptr);
		}

		connection.RemoveRequest(*this);
	}

	void Destroy() noexcept {
		this->~Request();
	}

	void DestroyEof() noexcept {
		auto *rbc = response_body_control;
		Destroy();
		rbc->SetEof();
	}

	void AbortError(std::exception_ptr e) noexcept;

	void SendRequest(http_method_t method, const char *uri,
			 StringMap &&headers,
			 UnusedIstreamPtr body) noexcept;

	int SubmitResponse(bool has_response_body) noexcept;

	int OnEndDataFrame() noexcept;

	int OnStreamCloseCallback(uint32_t error_code) noexcept;

	static int OnStreamCloseCallback(nghttp2_session *session,
					 int32_t stream_id,
					 uint32_t error_code, void *) noexcept {
		auto *request = (Request *)
			nghttp2_session_get_stream_user_data(session, stream_id);
		if (request == nullptr)
			return 0;

		return request->OnStreamCloseCallback(error_code);
	}

	int OnHeaderCallback(StringView name, StringView value) noexcept;

	static int OnHeaderCallback(nghttp2_session *session,
				    const nghttp2_frame *frame,
				    const uint8_t *name, size_t namelen,
				    const uint8_t *value, size_t valuelen,
				    uint8_t, void *) noexcept {
		if (frame->hd.type != NGHTTP2_HEADERS ||
		    frame->headers.cat != NGHTTP2_HCAT_RESPONSE)
			return 0;

		auto *request = (Request *)
			nghttp2_session_get_stream_user_data(session, frame->hd.stream_id);
		if (request == nullptr)
			return 0;

		return request->OnHeaderCallback({(const char *)name, namelen},
						 {(const char *)value, valuelen});
	}

	int OnDataChunkReceivedCallback(ConstBuffer<uint8_t> data,
					uint8_t flags) noexcept;

	static int OnDataChunkReceivedCallback(nghttp2_session *session,
					       uint8_t flags,
					       int32_t stream_id,
					       const uint8_t *data,
					       size_t len,
					       [[maybe_unused]] void *user_data) noexcept {
#ifndef NDEBUG
		auto &c = *(ClientConnection *)user_data;
		c.unconsumed += len;
#endif

		auto *request = (Request *)
			nghttp2_session_get_stream_user_data(session, stream_id);
		if (request == nullptr) {
#ifndef NDEBUG
			c.unconsumed -= len;
#endif
			nghttp2_session_consume(session, stream_id, len);
			return 0;
		}

		return request->OnDataChunkReceivedCallback({data, len}, flags);
	}

private:
	void Consume(size_t nbytes) noexcept {
#ifndef NDEBUG
		assert(connection.unconsumed >= nbytes);
		connection.unconsumed -= nbytes;
#endif

		nghttp2_session_consume(connection.session.get(), id, nbytes);
	}

	void AbortResponseHeaders(std::exception_ptr e) noexcept {
		auto &_handler = handler;
		Destroy();
		_handler.InvokeError(std::move(e));
	}

	void AbortResponseBody(std::exception_ptr e) noexcept {
		Consume(response_body_control->GetAvailable());
		response_body_control->DestroyError(std::move(e));
		response_body_control = nullptr;
		Destroy();
	}

	nghttp2_data_provider MakeRequestDataProvider(UnusedIstreamPtr &&istream) noexcept {
		assert(!request_body);
		assert(istream);

		IstreamDataSourceHandler &h = *this;
		request_body = std::make_unique<IstreamDataSource>(std::move(istream),
								   h);
		return request_body->MakeDataProvider();
	}

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override;

	/* virtual methods from class MultiFifoBufferIstreamHandler */
	void OnFifoBufferIstreamConsumed(size_t nbytes) noexcept override {
		Consume(nbytes);
	}

	void OnFifoBufferIstreamClosed() noexcept override {
		nghttp2_submit_rst_stream(connection.session.get(),
					  NGHTTP2_FLAG_NONE,
					  id, NGHTTP2_CANCEL);
		connection.socket->ScheduleWrite();
		Destroy();
	}

	/* virtual methods from class IstreamDataSourceHandler */
	void OnIstreamDataSourceReady() noexcept override {
		assert(request_body);
		assert(connection.socket);

		nghttp2_session_resume_data(connection.session.get(), id);
		connection.socket->ScheduleWrite();
	}
};

void
ClientConnection::Request::AbortError(std::exception_ptr e) noexcept
{
	switch (state) {
	case State::INITIAL:
		Destroy();
		break;

	case State::HEADERS:
		AbortResponseHeaders(std::move(e));
		break;

	case State::BODY:
		AbortResponseBody(std::move(e));
		break;
	}
}

inline void
ClientConnection::Request::SendRequest(http_method_t method, const char *uri,
				       StringMap &&headers,
				       UnusedIstreamPtr body) noexcept
{
	assert(state == State::INITIAL);

	StaticArray<nghttp2_nv, 256> hdrs;
	hdrs.push_back(MakeNv(":method", http_method_to_string(method)));
	hdrs.push_back(MakeNv(":scheme", "http")); // TODO

	const char *host = headers.Remove("host");
	if (host != nullptr)
		hdrs.push_back(MakeNv(":authority", host));

	hdrs.push_back(MakeNv(":path", uri));

	char content_length_string[32];
	if (body) {
		const auto content_length = body.GetAvailable(false);
		if (content_length >= 0) {
			snprintf(content_length_string,
				 sizeof(content_length_string),
				 "%lu", (unsigned long)content_length);
			hdrs.push_back(MakeNv("content-length",
					      content_length_string));
		}
	}

	for (const auto &i : headers)
		hdrs.push_back(MakeNv(i.key, i.value));

	nghttp2_data_provider dp, *dpp = nullptr;
	if (body) {
		dp = MakeRequestDataProvider(std::move(body));
		dpp = &dp;
	}

	id = nghttp2_submit_request(connection.session.get(), nullptr,
				    hdrs.raw(), hdrs.size(),
				    dpp,
				    this);
	if (id < 0) {
		AbortResponseHeaders(std::make_exception_ptr(FormatRuntimeError("nghttp2_submit_request() failed: %s",
										nghttp2_strerror(id))));
		return;
	}

	state = State::HEADERS;

	connection.socket->ScheduleWrite();
}

void
ClientConnection::Request::Cancel() noexcept
{
	nghttp2_submit_rst_stream(connection.session.get(), NGHTTP2_FLAG_NONE,
				  id, NGHTTP2_CANCEL);
	connection.socket->ScheduleWrite();
	Destroy();
}

inline int
ClientConnection::Request::OnHeaderCallback(StringView name,
					    StringView value) noexcept
{
	AllocatorPtr alloc(pool);

	if (name.Equals(":status")) {
		char buffer[4];
		if (value.size != 3)
			return 0;

		memcpy(buffer, value.data, value.size);
		buffer[value.size] = 0;

		char *endptr;
		auto _status = (http_status_t)strtoul(buffer, &endptr, 10);
		if (endptr != buffer + value.size ||
		    !http_status_is_valid(_status))
			return 0;

		status = _status;
	}

	if (name.size >= 2 && name.front() != ':')
		response_headers.Add(alloc, alloc.DupZ(name), alloc.DupZ(value));

	return 0;
}

inline int
ClientConnection::Request::OnDataChunkReceivedCallback(ConstBuffer<uint8_t> data,
						       uint8_t flags) noexcept
{
	// TODO: limit the MultiFifoBuffer size

	if (!response_body_control) {
		Consume(data.size);
		return 0;
	}

	response_body_control->Push(data.ToVoid());

	eof = (flags & NGHTTP2_FLAG_END_STREAM) != 0;

	if (eof) {
		DestroyEof();
		return 0;
	}

	response_body_control->SubmitBuffer();

	return 0;
}

int
ClientConnection::Request::SubmitResponse(bool has_response_body) noexcept
{
	UnusedIstreamPtr body;

	if (has_response_body) {
		MultiFifoBufferIstreamHandler &fbi_handler = *this;
		response_body_control = NewFromPool<MultiFifoBufferIstream>(pool, pool, fbi_handler);
		body = UnusedIstreamPtr(response_body_control);

		const char *content_length =
			response_headers.Remove("content-length");
		if (content_length != nullptr) {
			char *endptr;
			auto length = strtoul(content_length, &endptr, 10);
			if (endptr > content_length)
				body = NewIstreamPtr<LengthIstream>(pool,
								    std::move(body),
								    length);
		}
	}

	state = State::BODY;

	handler.InvokeResponse(status, std::move(response_headers),
			       std::move(body));

	if (!has_response_body)
		Destroy();

	return 0;
}

int
ClientConnection::Request::OnEndDataFrame() noexcept
{
	if (!response_body_control || eof)
		return 0;

	eof = true;

	DestroyEof();

	return 0;
}

int
ClientConnection::Request::OnStreamCloseCallback(uint32_t error_code) noexcept
{
	auto error = FormatRuntimeError("Stream closed: %s",
					nghttp2_http2_strerror(error_code));
	AbortError(std::make_exception_ptr(std::move(error)));
	return 0;
}

ClientConnection::ClientConnection(EventLoop &loop,
				   std::unique_ptr<FilteredSocket> _socket,
				   ConnectionHandler &_handler)
	:socket(std::move(_socket)),
	 handler(_handler),
	 defer_invoke_idle(loop, BIND_THIS_METHOD(InvokeIdle))
{
	socket->Reinit(Event::Duration(-1), write_timeout,
		       *this);

	NgHttp2::Option option;
	nghttp2_option_set_no_auto_window_update(option.get(), true);

	NgHttp2::SessionCallbacks callbacks;
	nghttp2_session_callbacks_set_send_callback(callbacks.get(), SendCallback);
	nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks.get(),
							     OnFrameRecvCallback);
	nghttp2_session_callbacks_set_on_stream_close_callback(callbacks.get(),
							       Request::OnStreamCloseCallback);
	nghttp2_session_callbacks_set_on_header_callback(callbacks.get(),
							 Request::OnHeaderCallback);
	nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks.get(),
								  Request::OnDataChunkReceivedCallback);

	session = NgHttp2::Session::NewClient(callbacks.get(), this,
					      option.get());

	static constexpr nghttp2_settings_entry iv[] = {
		{NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, MAX_CONCURRENT_STREAMS},
		{NGHTTP2_SETTINGS_ENABLE_PUSH, false},
	};

	const auto rv = nghttp2_submit_settings(session.get(), NGHTTP2_FLAG_NONE,
						iv, std::size(iv));
	if (rv != 0)
		throw FormatRuntimeError("nghttp2_submit_settings() failed: %s",
					 nghttp2_strerror(rv));

	socket->ScheduleWrite();
	socket->ScheduleReadNoTimeout(false);
}

ClientConnection::~ClientConnection() noexcept
{
	/* all requests must be finished/canceled before this object
	   gets destructed */
	assert(requests.empty());
	assert(unconsumed == 0);
}

void
ClientConnection::SendRequest(struct pool &request_pool,
			      StopwatchPtr stopwatch,
			      http_method_t method, const char *uri,
			      StringMap &&headers,
			      UnusedIstreamPtr body,
			      HttpResponseHandler &_handler,
			      CancellablePointer &cancel_ptr) noexcept
{
	auto *request = NewFromPool<Request>(request_pool, request_pool,
					     std::move(stopwatch), *this,
					     _handler, cancel_ptr);
	requests.push_front(*request);
	defer_invoke_idle.Cancel();
	request->SendRequest(method, uri, std::move(headers), std::move(body));
}

void
ClientConnection::RemoveRequest(Request &request) noexcept
{
	requests.erase(requests.iterator_to(request));

	if (requests.empty()) {
		assert(unconsumed == 0);
		defer_invoke_idle.ScheduleIdle();
	}
}

void
ClientConnection::AbortAllRequests(std::exception_ptr e) noexcept
{
	while (!requests.empty())
		requests.front().AbortError(e);
}

ssize_t
ClientConnection::SendCallback(const void *data, size_t length) noexcept
{
	const auto nbytes = socket->Write(data, length);
	if (nbytes < 0) {
		if (nbytes == WRITE_BLOCKING)
			return NGHTTP2_ERR_WOULDBLOCK;
		else
			return NGHTTP2_ERR_CALLBACK_FAILURE;
	}

	return nbytes;
}

int
ClientConnection::OnFrameRecvCallback(const nghttp2_frame *frame) noexcept
{
	switch (frame->hd.type) {
	case NGHTTP2_HEADERS:
		if (frame->hd.flags & NGHTTP2_FLAG_END_HEADERS) {
			void *stream_data = nghttp2_session_get_stream_user_data(session.get(),
										 frame->hd.stream_id);
			if (stream_data == nullptr)
				return 0;

			auto &request = *(Request *)stream_data;
			return request.SubmitResponse((frame->hd.flags & NGHTTP2_FLAG_END_STREAM) == 0);
		}
		break;

	case NGHTTP2_DATA:
		if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
			void *stream_data = nghttp2_session_get_stream_user_data(session.get(),
										 frame->hd.stream_id);
			if (stream_data == nullptr)
				return 0;

			auto &request = *(Request *)stream_data;
			return request.OnEndDataFrame();
		}

		break;

	case NGHTTP2_SETTINGS:
		for (size_t i = 0; i < frame->settings.niv; ++i)
			if (frame->settings.iv[i].settings_id == NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS)
				max_concurrent_streams = std::min<size_t>(frame->settings.iv[i].value,
									  MAX_CONCURRENT_STREAMS);
		break;

	case NGHTTP2_GOAWAY:
		handler.OnNgHttp2ConnectionGoAway();
		break;

	default:
		break;
	}
	return 0;
}

BufferedResult
ClientConnection::OnBufferedData()
{
	auto r = socket->ReadBuffer();

	auto nbytes = nghttp2_session_mem_recv(session.get(),
					       (const uint8_t *)r.data, r.size);
	if (nbytes < 0)
		throw FormatRuntimeError("nghttp2_session_mem_recv() failed: %s",
					 nghttp2_strerror((int)nbytes));

	socket->DisposeConsumed(nbytes);

	const auto rv = nghttp2_session_send(session.get());
	if (rv != 0)
		throw FormatRuntimeError("nghttp2_session_send() failed: %s",
					 nghttp2_strerror(rv));

	return BufferedResult::MORE; // TODO?
}

bool
ClientConnection::OnBufferedClosed() noexcept
{
	AbortAllRequests(std::make_exception_ptr(std::runtime_error("Peer closed the socket prematurely")));

	handler.OnNgHttp2ConnectionClosed();
	return false;
}

bool
ClientConnection::OnBufferedWrite()
{
	const auto rv = nghttp2_session_send(session.get());
	if (rv != 0)
		throw FormatRuntimeError("nghttp2_session_send() failed: %s",
					 nghttp2_strerror(rv));

	if (!nghttp2_session_want_write(session.get()))
		socket->UnscheduleWrite();

	return true;
}

void
ClientConnection::OnBufferedError(std::exception_ptr e) noexcept
{
	AbortAllRequests(e);

	handler.OnNgHttp2ConnectionError(std::move(e));
}

} // namespace NgHttp2
