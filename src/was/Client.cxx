// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Client.hxx"
#include "MetricsHandler.hxx"
#include "Map.hxx"
#include "Output.hxx"
#include "Input.hxx"
#include "Lease.hxx"
#include "was/async/Control.hxx"
#include "http/ResponseHandler.hxx"
#include "istream/istream_null.hxx"
#include "istream/UnusedPtr.hxx"
#include "strmap.hxx"
#include "pool/pool.hxx"
#include "pool/LeakDetector.hxx"
#include "stopwatch.hxx"
#include "lib/fmt/ToBuffer.hxx"
#include "net/SocketProtocolError.hxx"
#include "io/FileDescriptor.hxx"
#include "event/FineTimerEvent.hxx"
#include "http/HeaderLimits.hxx"
#include "http/HeaderName.hxx"
#include "http/Method.hxx"
#include "util/Cancellable.hxx"
#include "util/CharUtil.hxx"
#include "util/DestructObserver.hxx"
#include "util/Exception.hxx"
#include "util/SpanCast.hxx"
#include "util/StringSplit.hxx"
#include "util/ScopeExit.hxx"
#include "util/Unaligned.hxx"
#include "AllocatorPtr.hxx"

#include <was/protocol.h>

#include <algorithm> // for std::all_of()
#include <cassert>
#include <cmath> // for std::isnormal()

class WasClient final
	: Was::ControlHandler, WasOutputHandler, WasInputHandler,
	  DestructAnchor, PoolLeakDetector,
	  Cancellable
{
	const AllocatorPtr alloc;
	struct pool &caller_pool;

	const StopwatchPtr stopwatch;

	WasLease &lease;

	Was::Control control;

	WasMetricsHandler *const metrics_handler;

	HttpResponseHandler &handler;

	/**
	 * When we don't known the response length yet, this timer is
	 * used to delay submitting the response to
	 * #HttpResponseHandler a bit.  Chances are that we'll receive
	 * a WAS_COMMAND_LENGTH packet meanwhile (which can allow
	 * forwarding this response without HTTP/1.1 chunking), and if
	 * not, we're going to continue without a length.
	 */
	FineTimerEvent submit_response_timer;

	struct Request {
		WasOutput *body;

		explicit Request(WasOutput *_body) noexcept:body(_body) {}

		void ClearBody() noexcept {
			if (body != nullptr)
				was_output_free_p(&body);
		}
	} request;

	struct Response {
		HttpStatus status = HttpStatus::OK;

		/**
		 * Response headers being assembled.
		 */
		StringMap headers;

		std::size_t total_header_size = 0;

		WasInput *body;

		bool receiving_metadata = true;

		/**
		 * If set, then the invocation of the response handler is
		 * postponed, until the remaining control packets have been
		 * evaluated.
		 */
		bool pending = false;

		/**
		 * If set, then the response body length has been
		 * announced via WAS_COMMAND_LENGTH.
		 */
		bool known_length = false;

		/**
		 * Did the #WasInput release its pipe yet?  If this happens
		 * before the response is pending, then the response body must
		 * be empty.
		 */
		bool released = false;

		explicit Response(WasInput *_body) noexcept
			:body(_body) {}

		/**
		 * Are we currently receiving response metadata (such as
		 * headers)?
		 */
		bool IsReceivingMetadata() const noexcept {
			return receiving_metadata && !pending;
		}

		/**
		 * Has the response been submitted to the response handler?
		 */
		bool WasSubmitted() const noexcept {
			return !receiving_metadata;
		}
	} response;

	/**
	 * This is set to true while the final STOP is being sent to avoid
	 * recursive errors.
	 */
	bool ignore_control_errors = false;

public:
	WasClient(struct pool &_pool, struct pool &_caller_pool,
		  EventLoop &event_loop,
		  StopwatchPtr &&_stopwatch,
		  SocketDescriptor control_fd,
		  FileDescriptor input_fd, FileDescriptor output_fd,
		  WasLease &_lease,
		  HttpMethod method, UnusedIstreamPtr body,
		  WasMetricsHandler *_metrics_handler,
		  HttpResponseHandler &_handler,
		  CancellablePointer &cancel_ptr) noexcept;

	void SendRequest(const char *remote_host,
			 HttpMethod method, const char *uri,
			 const char *script_name, const char *path_info,
			 const char *query_string,
			 const StringMap &headers,
			 std::span<const char *const> params) noexcept;

private:
	void Destroy() noexcept {
		this->~WasClient();
	}

	template<typename B>
	void DestroyInvokeResponse(HttpStatus status, StringMap headers,
				   B &&body) noexcept {
		auto &_handler = handler;
		Destroy();
		_handler.InvokeResponse(status, std::move(headers),
					std::forward<B>(body));
	}

	void DestroyInvokeError(std::exception_ptr ep) noexcept {
		auto &_handler = handler;
		Destroy();
		_handler.InvokeError(ep);
	}

	/**
	 * Cancel the request body by sending #WAS_COMMAND_PREMATURE to
	 * the WAS child process.
	 *
	 * @return false on error (OnWasControlError() has been called).
	 */
	bool CancelRequestBody() noexcept {
		if (request.body == nullptr)
			return true;

		uint64_t sent = was_output_free_p(&request.body);
		return control.SendUint64(WAS_COMMAND_PREMATURE, sent) &&
			control.FlushOutput();
	}

	/**
	 * Release the control channel and invoke WasLease::ReleaseWas().
	 * If the control channel is clean (i.e. buffers are empty), it
	 * will attempt to reuse the WAS child process.
	 *
	 * Prior to calling this method, the #WasInput and the #WasOutput
	 * must be released already.
	 */
	void ReleaseControl() noexcept {
		assert(request.body == nullptr);
		assert(response.body == nullptr || response.released);

		if (!control.IsDefined())
			/* already released */
			return;

		const PutAction action = control.empty()
			? PutAction::REUSE
			: PutAction::DESTROY;

		control.ReleaseSocket();

		lease.ReleaseWas(action);
	}

	/**
	 * @return false on error (OnWasControlError() has been called).
	 */
	bool ReleaseControlStop(uint64_t received) noexcept {
		assert(response.body == nullptr);

		if (!control.IsDefined())
			/* already released */
			return true;

		request.ClearBody();

		/* if an error occurs while sending STOP, don't pass it to our
		   handler - he's not interested anymore */
		ignore_control_errors = true;

		if (!control.Send(WAS_COMMAND_STOP) ||
		    !control.FlushOutput())
			return false;

		control.ReleaseSocket();

		lease.ReleaseWasStop(received);

		return true;
	}

	/**
	 * Destroys the objects Was::Control, WasInput, WasOutput and
	 * releases the socket lease.  Assumes the response body has not
	 * been enabled.
	 */
	void ClearUnused() noexcept {
		request.ClearBody();

		if (response.body != nullptr)
			was_input_free_unused_p(&response.body);

		if (control.IsDefined())
			control.ReleaseSocket();

		lease.ReleaseWas(PutAction::DESTROY);
	}

	/**
	 * Abort receiving the response status/headers from the WAS server.
	 */
	void AbortResponseHeaders(std::exception_ptr ep) noexcept {
		assert(response.IsReceivingMetadata());

		ClearUnused();

		DestroyInvokeError(ep);
	}

	/**
	 * Abort receiving the response body from the WAS server.
	 */
	void AbortResponseBody(std::exception_ptr ep) noexcept {
		assert(response.WasSubmitted());

		request.ClearBody();

		auto *response_body = std::exchange(response.body, nullptr);
		if (response_body != nullptr)
			/* cancel the SocketEvent before releasing the WAS
			   process lease */
			was_input_disable(*response_body);

		if (control.IsDefined())
			control.ReleaseSocket();

		lease.ReleaseWas(PutAction::DESTROY);

		Destroy();

		if (response_body != nullptr)
			was_input_free(response_body, ep);
	}

	/**
	 * Call this when end of the response body has been seen.  It will
	 * take care of releasing the #WasClient.
	 */
	void ResponseEof() noexcept {
		assert(response.WasSubmitted());
		assert(response.body == nullptr);

		if (!CancelRequestBody())
			return;

		ReleaseControl();
		Destroy();
	}

	/**
	 * Abort a pending response (BODY has been received, but the response
	 * handler has not yet been invoked).
	 */
	void AbortPending(std::exception_ptr ep) noexcept {
		assert(!response.IsReceivingMetadata() &&
		       !response.WasSubmitted());

		ClearUnused();

		DestroyInvokeError(ep);
	}

	/**
	 * Abort receiving the response status/headers from the WAS server.
	 */
	void AbortResponse(std::exception_ptr ep) noexcept {
		if (response.IsReceivingMetadata())
			AbortResponseHeaders(ep);
		else if (response.WasSubmitted())
			AbortResponseBody(ep);
		else
			AbortPending(ep);
	}

	/**
	 * Submit the pending response to our handler.
	 *
	 * @return false if our #Was::Control instance has been disposed
	 */
	bool SubmitPendingResponse() noexcept;

	void OnSubmitResponseTimer() noexcept;

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		/* Cancellable::Cancel() can only be used before the
		   response was delivered to our callback */
		assert(!response.WasSubmitted());

		stopwatch.RecordEvent("cancel");

		/* if an error occurs while sending PREMATURE, don't pass it
		   to our handler - he's not interested anymore */
		ignore_control_errors = true;

		if (!CancelRequestBody())
			return;

		if (response.body != nullptr)
			was_input_free_unused_p(&response.body);

		if (!ReleaseControlStop(0))
			return;

		Destroy();
	}

	/* virtual methods from class WasControlHandler */
	bool OnWasControlPacket(enum was_command cmd,
				std::span<const std::byte> payload) noexcept override;
	bool OnWasControlDrained() noexcept override;

	void OnWasControlDone() noexcept override {
		assert(request.body == nullptr);
		assert(response.body == nullptr);
		assert(control.IsDefined());
	}

	void OnWasControlHangup() noexcept override {
		assert(control.IsDefined());

		OnWasControlError(std::make_exception_ptr(SocketClosedPrematurelyError{}));
	}

	void OnWasControlError(std::exception_ptr ep) noexcept override {
		assert(control.IsDefined());

		if (ignore_control_errors) {
			ClearUnused();
			Destroy();
			return;
		}

		stopwatch.RecordEvent("control_error");

		AbortResponse(NestException(ep,
					    std::runtime_error("Error on WAS control channel")));
	}

	/* virtual methods from class WasOutputHandler */
	bool WasOutputLength(uint64_t length) noexcept override;
	bool WasOutputPremature(uint64_t length,
				std::exception_ptr ep) noexcept override;
	void WasOutputEof() noexcept override;
	void WasOutputError(std::exception_ptr ep) noexcept override;

	/* virtual methods from class WasInputHandler */
	void WasInputClose(uint64_t received) noexcept override;
	bool WasInputRelease() noexcept override;
	void WasInputEof() noexcept override;
	void WasInputError() noexcept override;
};

bool
WasClient::SubmitPendingResponse() noexcept
{
	assert(response.pending);
	assert(!response.WasSubmitted());

	/* just in case WAS_COMMAND_LENGTH was received while the
	   submit_response_timer was pending */
	submit_response_timer.Cancel();

	stopwatch.RecordEvent("headers");

	response.pending = false;

	response.receiving_metadata = false;

	if (response.released) {
		was_input_free_unused_p(&response.body);
		ReleaseControl();

		DestroyInvokeResponse(response.status, std::move(response.headers),
				      istream_null_new(caller_pool));
		return false;
	} else {
		const DestructObserver destructed(*this);
		handler.InvokeResponse(response.status, std::move(response.headers),
				       was_input_enable(*response.body));
		return !destructed && control.IsDefined();
	}
}

inline void
WasClient::OnSubmitResponseTimer() noexcept
{
	/* we have response metadata, but after this timeout, we still
	   havn't received WAS_COMMAND_LENGTH - give up and submit the
	   response without a known total length */

	SubmitPendingResponse();
}

/*
 * Was::ControlHandler
 */

static constexpr bool
IsValidHeaderValueChar(char ch) noexcept
{
	return ch != '\0' && ch != '\n' && ch != '\r';
}

static constexpr bool
IsValidHeaderValue(std::string_view value) noexcept
{
	return std::all_of(value.begin(), value.end(), IsValidHeaderValueChar);
}

static void
ParseHeaderPacket(AllocatorPtr alloc, StringMap &headers,
		  std::string_view payload)
{
	const auto [name, value] = Split(payload, '=');

	if (value.data() == nullptr || !http_header_name_valid(name) ||
	    !IsValidHeaderValue(value))
		throw SocketProtocolError{"Malformed WAS HEADER packet"};

	headers.Add(alloc, alloc.DupToLower(name), alloc.DupZ(value));
}

static bool
IsValidMetricName(std::string_view name) noexcept
{
	return !name.empty() && name.size() < 64 &&
		std::all_of(name.begin(), name.end(), [](char ch){
			return IsAlphaNumericASCII(ch) || ch == '_';
		});
}

static bool
HandleMetric(WasMetricsHandler &handler,
	     std::span<const std::byte> payload)
{
	const float &value = *(const float *)(const void *)payload.data();

	if (payload.size() <= sizeof(value))
		return false;

	if (!std::isfinite(value))
		return false;

	const auto name = ToStringView(payload.subspan(sizeof(value)));
	if (!IsValidMetricName(name))
		return false;

	handler.OnWasMetric(name, value);

	return true;
}

bool
WasClient::OnWasControlPacket(enum was_command cmd,
			      std::span<const std::byte> payload) noexcept
{
	switch (cmd) {
		HttpStatus status;

	case WAS_COMMAND_NOP:
		break;

	case WAS_COMMAND_REQUEST:
	case WAS_COMMAND_URI:
	case WAS_COMMAND_METHOD:
	case WAS_COMMAND_SCRIPT_NAME:
	case WAS_COMMAND_PATH_INFO:
	case WAS_COMMAND_QUERY_STRING:
	case WAS_COMMAND_PARAMETER:
	case WAS_COMMAND_REMOTE_HOST:
		stopwatch.RecordEvent("control_error");
		AbortResponse(std::make_exception_ptr(SocketProtocolError(FmtBuffer<64>("Unexpected WAS packet {}",
										     static_cast<unsigned>(cmd)))));
		return false;

	case WAS_COMMAND_HEADER:
		if (!response.IsReceivingMetadata()) {
			stopwatch.RecordEvent("control_error");
			AbortResponse(std::make_exception_ptr(SocketProtocolError{"response header was too late"}));
			return false;
		}

		try {
			if (payload.size() >= MAX_HTTP_HEADER_SIZE)
				throw SocketProtocolError{"Response header is too long"};

			response.total_header_size += payload.size();
			if (response.total_header_size >= MAX_TOTAL_HTTP_HEADER_SIZE)
				throw SocketProtocolError{"Too many response headers"};

			ParseHeaderPacket(alloc, response.headers,
					  ToStringView(payload));
		} catch (...) {
			stopwatch.RecordEvent("control_error");
			AbortResponseHeaders(std::current_exception());
			return false;
		}

		break;

	case WAS_COMMAND_STATUS:
		if (!response.IsReceivingMetadata()) {
			stopwatch.RecordEvent("control_error");
			/* note: using AbortResponse() instead of
			   AbortResponseBody() because the response may be still
			   "pending" */
			AbortResponse(std::make_exception_ptr(SocketProtocolError("STATUS after body start")));
			return false;
		}

		if (payload.size() == sizeof(uint32_t))
			status = static_cast<HttpStatus>(LoadUnaligned<uint32_t>(payload.data()));
		else if (payload.size() == sizeof(uint16_t))
			status = static_cast<HttpStatus>(LoadUnaligned<uint16_t>(payload.data()));
		else {
			stopwatch.RecordEvent("control_error");
			AbortResponseHeaders(std::make_exception_ptr(SocketProtocolError("malformed STATUS")));
			return false;
		}

		if (!http_status_is_valid(status)) {
			stopwatch.RecordEvent("control_error");
			AbortResponseHeaders(std::make_exception_ptr(SocketProtocolError("malformed STATUS")));
			return false;
		}

		response.status = status;

		if (http_status_is_empty(response.status) &&
		    response.body != nullptr)
			/* no response body possible with this status; release the
			   object */
			was_input_free_unused_p(&response.body);

		break;

	case WAS_COMMAND_NO_DATA:
		if (!response.IsReceivingMetadata()) {
			stopwatch.RecordEvent("control_error");
			AbortResponseBody(std::make_exception_ptr(SocketProtocolError("NO_DATA after body start")));
			return false;
		}

		response.receiving_metadata = false;

		if (response.body != nullptr)
			was_input_free_unused_p(&response.body);

		if (!CancelRequestBody())
			return false;

		ReleaseControl();

		DestroyInvokeResponse(response.status, std::move(response.headers),
				      UnusedIstreamPtr());
		return false;

	case WAS_COMMAND_DATA:
		if (!response.IsReceivingMetadata()) {
			stopwatch.RecordEvent("control_error");
			AbortResponseBody(std::make_exception_ptr(SocketProtocolError("DATA after body start")));
			return false;
		}

		if (response.body == nullptr) {
			stopwatch.RecordEvent("control_error");
			AbortResponseHeaders(std::make_exception_ptr(SocketProtocolError("no response body allowed")));
			return false;
		}

		response.pending = true;
		break;

	case WAS_COMMAND_LENGTH:
		if (response.IsReceivingMetadata()) {
			stopwatch.RecordEvent("control_error");
			AbortResponseHeaders(std::make_exception_ptr(SocketProtocolError("LENGTH before DATA")));
			return false;
		}

		if (response.body == nullptr) {
			stopwatch.RecordEvent("control_error");
			AbortResponseBody(std::make_exception_ptr(SocketProtocolError("LENGTH after NO_DATA")));
			return false;
		}

		if (payload.size() != sizeof(uint64_t)) {
			stopwatch.RecordEvent("control_error");
			AbortResponseBody(std::make_exception_ptr(SocketProtocolError("malformed LENGTH packet")));
			return false;
		}

		if (!was_input_set_length(response.body,
					  LoadUnaligned<uint64_t>(payload.data())))
			return false;

		if (!control.IsDefined()) {
			/* through WasInputRelease(), the above
			   was_input_set_length() call may have disposed the
			   Was::Control instance; this condition needs to be
			   reported to our caller */

			if (response.pending)
				/* since OnWasControlDrained() isn't going to be
				   called (because we cancelled that), we need to do
				   this check manually */
				SubmitPendingResponse();

			return false;
		}

		/* now that we know the length, we can finally submit
		   the response (and don't need to wait for
		   submit_response_timer to trigger that) */
		response.known_length = true;
		break;

	case WAS_COMMAND_STOP:
		return CancelRequestBody();

	case WAS_COMMAND_PREMATURE:
		if (response.IsReceivingMetadata()) {
			stopwatch.RecordEvent("control_error");
			AbortResponseHeaders(std::make_exception_ptr(SocketProtocolError("PREMATURE before DATA")));
			return false;
		}

		if (payload.size() != sizeof(uint64_t)) {
			stopwatch.RecordEvent("control_error");
			AbortResponseBody(std::make_exception_ptr(SocketProtocolError("malformed PREMATURE packet")));
			return false;
		}

		if (response.body == nullptr)
			break;

		if (response.pending) {
			/* we can't let was_input report the error to its handler,
			   because it cannot possibly have a handler yet; thus
			   catch it and report it to the #HttpResponseHandler */
			try {
				AtScopeExit(this) { response.body = nullptr; };
				was_input_premature_throw(response.body,
							  LoadUnaligned<uint64_t>(payload.data()));
			} catch (...) {
				AbortPending(std::current_exception());
			}
		} else {
			was_input_premature(response.body,
					    LoadUnaligned<uint64_t>(payload.data()));
		}

		return false;

	case WAS_COMMAND_METRIC:
		if (metrics_handler != nullptr &&
		    !HandleMetric(*metrics_handler, payload)) {
			stopwatch.RecordEvent("control_error");
			AbortResponse(std::make_exception_ptr(SocketProtocolError{"Malformed METRIC packet"}));
			return false;
		}

		return true;
	}

	return true;
}

bool
WasClient::OnWasControlDrained() noexcept
{
	if (response.pending) {
		if (response.known_length)
			return SubmitPendingResponse();
		else {
			/* we don't know the length yet - wait a bit
			   before submitting the response, maybe we'll
			   receive WAS_COMMAND_LENGTH really soon */
			submit_response_timer.Schedule(std::chrono::milliseconds(5));
			return true;
		}
	} else
		return true;
}

/*
 * Output handler
 */

bool
WasClient::WasOutputLength(uint64_t length) noexcept
{
	assert(control.IsDefined());
	assert(request.body != nullptr);

	return control.SendUint64(WAS_COMMAND_LENGTH, length);
}

bool
WasClient::WasOutputPremature(uint64_t length, std::exception_ptr ep) noexcept
{
	assert(control.IsDefined());
	assert(request.body != nullptr);

	stopwatch.RecordEvent("request_error");

	request.body = nullptr;

	/* XXX send PREMATURE, recover */
	(void)length;

	AbortResponse(ep);
	return false;
}

void
WasClient::WasOutputEof() noexcept
{
	assert(request.body != nullptr);

	stopwatch.RecordEvent("request_end");

	request.body = nullptr;
}

void
WasClient::WasOutputError(std::exception_ptr ep) noexcept
{
	assert(request.body != nullptr);

	stopwatch.RecordEvent("send_error");

	request.body = nullptr;

	AbortResponse(ep);
}

/*
 * Input handler
 */

void
WasClient::WasInputClose(uint64_t received) noexcept
{
	assert(response.WasSubmitted());
	assert(response.body != nullptr);

	stopwatch.RecordEvent("close");

	response.body = nullptr;

	/* if an error occurs while sending PREMATURE, don't pass it
	   to our handler - he's not interested anymore */
	ignore_control_errors = true;

	if (!CancelRequestBody() ||
	    !ReleaseControlStop(received))
		return;

	Destroy();
}

bool
WasClient::WasInputRelease() noexcept
{
	assert(response.body != nullptr);
	assert(!response.released);

	stopwatch.RecordEvent("eof");

	response.released = true;

	if (!CancelRequestBody())
		return false;

	ReleaseControl();
	return true;
}

void
WasClient::WasInputEof() noexcept
{
	assert(response.WasSubmitted());
	assert(response.body != nullptr);
	assert(response.released);

	response.body = nullptr;

	ResponseEof();
}

void
WasClient::WasInputError() noexcept
{
	assert(response.WasSubmitted());
	assert(response.body != nullptr);

	stopwatch.RecordEvent("error");

	response.body = nullptr;

	if (control.IsDefined())
		control.ReleaseSocket();

	lease.ReleaseWas(PutAction::DESTROY);

	Destroy();
}

/*
 * constructor
 *
 */

inline
WasClient::WasClient(struct pool &_pool, struct pool &_caller_pool,
		     EventLoop &event_loop,
		     StopwatchPtr &&_stopwatch,
		     SocketDescriptor control_fd,
		     FileDescriptor input_fd, FileDescriptor output_fd,
		     WasLease &_lease,
		     HttpMethod method, UnusedIstreamPtr body,
		     WasMetricsHandler *_metrics_handler,
		     HttpResponseHandler &_handler,
		     CancellablePointer &cancel_ptr) noexcept
	:PoolLeakDetector(_pool),
	 alloc(_pool), caller_pool(_caller_pool),
	 stopwatch(std::move(_stopwatch)),
	 lease(_lease),
	 control(event_loop, control_fd, *this),
	 metrics_handler(_metrics_handler),
	 handler(_handler),
	 submit_response_timer(event_loop,
			       BIND_THIS_METHOD(OnSubmitResponseTimer)),
	 request(body
		 ? was_output_new(_pool, event_loop, output_fd,
				  std::move(body), *this)
		 : nullptr),
	 response(http_method_is_empty(method)
		  ? nullptr
		  : was_input_new(_pool, event_loop, input_fd, *this))
{
	cancel_ptr = *this;
}

static bool
SendRequest(Was::Control &control,
	    bool enable_metrics,
	    const char *remote_host,
	    HttpMethod method, const char *uri,
	    const char *script_name, const char *path_info,
	    const char *query_string,
	    const StringMap &headers, WasOutput *request_body,
	    std::span<const char *const> params)
{
	const uint32_t method32 = (uint32_t)method;

	return control.Send(WAS_COMMAND_REQUEST) &&
		(!enable_metrics || control.Send(WAS_COMMAND_METRIC)) &&
		(method == HttpMethod::GET ||
		 control.SendT(WAS_COMMAND_METHOD, method32)) &&
		control.SendString(WAS_COMMAND_URI, uri) &&
		(script_name == nullptr ||
		 control.SendString(WAS_COMMAND_SCRIPT_NAME, script_name)) &&
		(path_info == nullptr ||
		 control.SendString(WAS_COMMAND_PATH_INFO, path_info)) &&
		(query_string == nullptr ||
		 control.SendString(WAS_COMMAND_QUERY_STRING, query_string)) &&
		Was::SendMap(control, WAS_COMMAND_HEADER, headers) &&
		control.SendArray(WAS_COMMAND_PARAMETER, params) &&
		(remote_host == nullptr ||
		 control.SendString(WAS_COMMAND_REMOTE_HOST, remote_host)) &&
		control.Send(request_body != nullptr
			     ? WAS_COMMAND_DATA
			     : WAS_COMMAND_NO_DATA) &&
		(request_body == nullptr || was_output_check_length(*request_body));
}

inline void
WasClient::SendRequest(const char *remote_host,
		       HttpMethod method, const char *uri,
		       const char *script_name, const char *path_info,
		       const char *query_string,
		       const StringMap &headers,
		       std::span<const char *const> params) noexcept
{
	::SendRequest(control, metrics_handler != nullptr,
		      remote_host,
		      method, uri, script_name, path_info,
		      query_string, headers, request.body,
		      params);
}

void
was_client_request(struct pool &caller_pool, EventLoop &event_loop,
		   StopwatchPtr stopwatch,
		   SocketDescriptor control_fd,
		   FileDescriptor input_fd, FileDescriptor output_fd,
		   WasLease &lease,
		   const char *remote_host,
		   HttpMethod method, const char *uri,
		   const char *script_name, const char *path_info,
		   const char *query_string,
		   const StringMap &headers, UnusedIstreamPtr body,
		   std::span<const char *const> params,
		   WasMetricsHandler *metrics_handler,
		   HttpResponseHandler &handler,
		   CancellablePointer &cancel_ptr) noexcept
{
	assert(http_method_is_valid(method));
	assert(uri != nullptr);

	auto client = NewFromPool<WasClient>(caller_pool, caller_pool, caller_pool,
					     event_loop, std::move(stopwatch),
					     control_fd, input_fd, output_fd,
					     lease, method, std::move(body),
					     metrics_handler,
					     handler, cancel_ptr);
	client->SendRequest(remote_host,
			    method, uri, script_name, path_info,
			    query_string, headers,
			    params);
}
