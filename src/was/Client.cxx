// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

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
#include "event/DeferEvent.hxx"
#include "event/FineTimerEvent.hxx"
#include "http/HeaderLimits.hxx"
#include "http/HeaderName.hxx"
#include "http/Method.hxx"
#include "util/Cancellable.hxx"
#include "util/CharUtil.hxx"
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

bool
IsWasClientRetryFailure(std::exception_ptr error) noexcept
{
	if (FindNested<SocketClosedPrematurelyError>(error))
		return true;

	return false;
}

class WasClient final
	: Was::ControlHandler, WasOutputHandler, WasInputHandler,
	  PoolLeakDetector,
	  Cancellable
{
	const AllocatorPtr alloc;
	struct pool &caller_pool;

	const StopwatchPtr stopwatch;

	WasLease &lease;

	Was::Control &control;

	WasMetricsHandler *const metrics_handler;

	HttpResponseHandler &handler;

	/**
	 * This defers update calls to WasInput (e.g. length,
	 * premature) out of the OnWasControlPacket() method.  This is
	 * important because these calls may (indirectly) release or
	 * break the Was::Control instance in ways that we can't
	 * report to the OnWasControlPacket() caller.
	 */
	DeferEvent defer_update_input;

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
				was_output_free(std::exchange(body, nullptr));
		}
	} request;

	struct Response {
		HttpStatus status = HttpStatus::OK;

		/**
		 * Response headers being assembled.
		 */
		StringMap headers;

		uint_least64_t pending_size;

		std::size_t total_header_size = 0;

		WasInput *body;

		enum class PendingInputType : uint_least8_t {
			NONE,
			LENGTH,
			PREMATURE,
		} pending_input_type = PendingInputType::NONE;

		bool receiving_metadata = true;

		/**
		 * If set, then the invocation of the response handler is
		 * postponed, until the remaining control packets have been
		 * evaluated.
		 */
		bool pending = false;

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

	bool lease_released = false;

	/**
	 * This is set to true while the final STOP is being sent to avoid
	 * recursive errors.
	 */
	bool ignore_control_errors = false;

public:
	WasClient(struct pool &_pool, struct pool &_caller_pool,
		  StopwatchPtr &&_stopwatch,
		  Was::Control &_control,
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
		_handler.InvokeError(std::move(ep));
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

		uint64_t sent = was_output_free(std::exchange(request.body, nullptr));
		return control.SendUint64(WAS_COMMAND_PREMATURE, sent);
	}

	bool IsControlReleased() const noexcept {
		return lease_released;
	}

	/**
	 * Release the control channel and invoke WasLease::ReleaseWas().
	 * If the control channel is clean (i.e. buffers are empty), it
	 * will attempt to reuse the WAS child process.
	 *
	 * Prior to calling this method, the #WasInput and the #WasOutput
	 * must be released already.
	 */
	PutAction ReleaseControl() noexcept {
		assert(request.body == nullptr);
		assert(response.body == nullptr || response.released);

		if (IsControlReleased())
			/* already released */
			return PutAction::REUSE;

		lease_released = true;
		return lease.ReleaseWas(PutAction::REUSE);
	}

	/**
	 * @return false on error (OnWasControlError() has been called).
	 */
	bool ReleaseControlStop(uint64_t received) noexcept {
		assert(response.body == nullptr);

		if (IsControlReleased())
			/* already released */
			return true;

		request.ClearBody();

		/* if an error occurs while sending STOP, don't pass it to our
		   handler - he's not interested anymore */
		ignore_control_errors = true;

		if (!control.Send(WAS_COMMAND_STOP))
			return false;

		lease.ReleaseWasStop(received);
		lease_released = true;

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
			was_input_free_unused(std::exchange(response.body, nullptr));

		lease.ReleaseWas(PutAction::DESTROY);
		lease_released = true;
	}

	/**
	 * Abort receiving the response status/headers from the WAS server.
	 */
	void AbortResponseHeaders(std::exception_ptr ep) noexcept {
		assert(response.IsReceivingMetadata());

		ClearUnused();

		DestroyInvokeError(std::move(ep));
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

		lease.ReleaseWas(PutAction::DESTROY);
		lease_released = true;

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

		DestroyInvokeError(std::move(ep));
	}

	/**
	 * Abort receiving the response status/headers from the WAS server.
	 */
	void AbortResponse(std::exception_ptr ep) noexcept {
		if (response.IsReceivingMetadata())
			AbortResponseHeaders(std::move(ep));
		else if (response.WasSubmitted())
			AbortResponseBody(std::move(ep));
		else
			AbortPending(std::move(ep));
	}

	void AbortControlError(std::exception_ptr error) noexcept {
		if (ignore_control_errors) {
			ClearUnused();
			Destroy();
			return;
		}

		stopwatch.RecordEvent("control_error");

		AbortResponse(NestException(std::move(error),
					    std::runtime_error("Error on WAS control channel")));
	}

	/**
	 * Submit the pending response to our handler.
	 */
	void SubmitPendingResponse() noexcept;

	void OnSubmitResponseTimer() noexcept;
	void OnDeferredInputUpdate() noexcept;

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
			was_input_free_unused(std::exchange(response.body, nullptr));

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
		assert(!IsControlReleased());
	}

	void OnWasControlHangup() noexcept override {
		assert(!control.IsDefined());
		assert(!IsControlReleased());

		AbortControlError(std::make_exception_ptr(SocketClosedPrematurelyError{}));
	}

	void OnWasControlError(std::exception_ptr ep) noexcept override {
		assert(!IsControlReleased());

		AbortControlError(std::move(ep));
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

void
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
		/* must have been released already by
                   WasInputRelease() */
		assert(IsControlReleased());

		was_input_free_unused(std::exchange(response.body, nullptr));

		DestroyInvokeResponse(response.status, std::move(response.headers),
				      istream_null_new(caller_pool));
	} else {
		handler.InvokeResponse(response.status, std::move(response.headers),
				       was_input_enable(*response.body));
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

inline void
WasClient::OnDeferredInputUpdate() noexcept
{
	assert(response.body != nullptr);

	switch (response.pending_input_type) {
	case Response::PendingInputType::NONE:
		break;

	case Response::PendingInputType::LENGTH:
		if (!was_input_set_length(response.body, response.pending_size))
			return;

		if (response.pending)
			/* now that we know the length, we can finally
			   submit the response (and don't need to wait
			   for submit_response_timer to trigger
			   that) */
			SubmitPendingResponse();
		break;

	case Response::PendingInputType::PREMATURE:
		was_input_premature(response.body, response.pending_size);
		break;
	}
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
		PutAction put_action;

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
			was_input_free_unused(std::exchange(response.body, nullptr));

		break;

	case WAS_COMMAND_NO_DATA:
		if (!response.IsReceivingMetadata()) {
			stopwatch.RecordEvent("control_error");
			AbortResponseBody(std::make_exception_ptr(SocketProtocolError("NO_DATA after body start")));
			return false;
		}

		response.receiving_metadata = false;

		if (response.body != nullptr)
			was_input_free_unused(std::exchange(response.body, nullptr));

		if (!CancelRequestBody())
			return false;

		put_action = ReleaseControl();

		DestroyInvokeResponse(response.status, std::move(response.headers),
				      UnusedIstreamPtr());
		return put_action == PutAction::REUSE;

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

		if (response.pending_input_type >= Response::PendingInputType::LENGTH) {
			stopwatch.RecordEvent("control_error");
			AbortResponseBody(std::make_exception_ptr(SocketProtocolError("Misplaced LENGTH")));
			return false;
		}

		if (payload.size() != sizeof(uint64_t)) {
			stopwatch.RecordEvent("control_error");
			AbortResponseBody(std::make_exception_ptr(SocketProtocolError("malformed LENGTH packet")));
			return false;
		}

		response.pending_input_type = Response::PendingInputType::LENGTH;
		response.pending_size = LoadUnaligned<uint64_t>(payload.data());
		defer_update_input.Schedule();
		break;

	case WAS_COMMAND_STOP:
		return CancelRequestBody();

	case WAS_COMMAND_PREMATURE:
		if (response.IsReceivingMetadata()) {
			stopwatch.RecordEvent("control_error");
			AbortResponseHeaders(std::make_exception_ptr(SocketProtocolError("PREMATURE before DATA")));
			return false;
		}

		if (response.pending_input_type >= Response::PendingInputType::PREMATURE) {
			stopwatch.RecordEvent("control_error");
			AbortResponseBody(std::make_exception_ptr(SocketProtocolError("Misplaced PREMATURE")));
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

			return false;
		} else {
			response.pending_input_type = Response::PendingInputType::PREMATURE;
			response.pending_size = LoadUnaligned<uint64_t>(payload.data());
			defer_update_input.Schedule();
			return true;
		}

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
	if (response.pending &&
	    response.pending_input_type != Response::PendingInputType::LENGTH) {
		/* we don't know the length yet - wait a bit
		   before submitting the response, maybe we'll
		   receive WAS_COMMAND_LENGTH really soon */
		submit_response_timer.Schedule(std::chrono::milliseconds(5));
	}

	return true;
}

/*
 * Output handler
 */

bool
WasClient::WasOutputLength(uint64_t length) noexcept
{
	assert(!IsControlReleased());
	assert(request.body != nullptr);

	return control.SendUint64(WAS_COMMAND_LENGTH, length);
}

bool
WasClient::WasOutputPremature(uint64_t length, std::exception_ptr ep) noexcept
{
	assert(!IsControlReleased());
	assert(request.body != nullptr);

	stopwatch.RecordEvent("request_error");

	request.body = nullptr;

	/* XXX send PREMATURE, recover */
	(void)length;

	AbortResponse(std::move(ep));
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

	AbortResponse(std::move(ep));
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
	defer_update_input.Cancel();

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
	defer_update_input.Cancel();

	ResponseEof();
}

void
WasClient::WasInputError() noexcept
{
	assert(response.WasSubmitted());
	assert(response.body != nullptr);

	stopwatch.RecordEvent("error");

	response.body = nullptr;

	lease.ReleaseWas(PutAction::DESTROY);
	lease_released = true;

	Destroy();
}

/*
 * constructor
 *
 */

inline
WasClient::WasClient(struct pool &_pool, struct pool &_caller_pool,
		     StopwatchPtr &&_stopwatch,
		     Was::Control &_control,
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
	 control(_control),
	 metrics_handler(_metrics_handler),
	 handler(_handler),
	 defer_update_input(control.GetEventLoop(),
			    BIND_THIS_METHOD(OnDeferredInputUpdate)),
	 submit_response_timer(control.GetEventLoop(),
			       BIND_THIS_METHOD(OnSubmitResponseTimer)),
	 request(body
		 ? was_output_new(_pool, control.GetEventLoop(), output_fd,
				  std::move(body), *this)
		 : nullptr),
	 response(http_method_is_empty(method)
		  ? nullptr
		  : was_input_new(_pool, control.GetEventLoop(), input_fd, *this))
{
	cancel_ptr = *this;

	control.SetHandler(*this);
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
was_client_request(struct pool &caller_pool,
		   StopwatchPtr stopwatch,
		   Was::Control &control,
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
					     std::move(stopwatch),
					     control, input_fd, output_fd,
					     lease, method, std::move(body),
					     metrics_handler,
					     handler, cancel_ptr);
	client->SendRequest(remote_host,
			    method, uri, script_name, path_info,
			    query_string, headers,
			    params);
}
