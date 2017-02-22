/*
 * Web Application Socket client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "was_client.hxx"
#include "was_quark.h"
#include "was_control.hxx"
#include "was_output.hxx"
#include "was_input.hxx"
#include "Lease.hxx"
#include "http_response.hxx"
#include "direct.hxx"
#include "istream/istream_null.hxx"
#include "strmap.hxx"
#include "pool.hxx"
#include "stopwatch.hxx"
#include "util/Cast.hxx"
#include "util/ConstBuffer.hxx"
#include "util/Cancellable.hxx"

#include <was/protocol.h>

#include <errno.h>
#include <string.h>
#include <sys/socket.h>

struct WasClient final : WasControlHandler, WasOutputHandler, WasInputHandler, Cancellable {
    struct pool &pool, &caller_pool;

    Stopwatch *const stopwatch;

    WasLease &lease;

    WasControl control;

    HttpResponseHandler &handler;

    struct Request {
        WasOutput *body;

        explicit Request(WasOutput *_body):body(_body) {}

        void ClearBody() {
            if (body != nullptr)
                was_output_free_p(&body);
        }
    } request;

    struct Response {
        http_status_t status = HTTP_STATUS_OK;

        /**
         * Response headers being assembled.
         */
        StringMap headers;

        WasInput *body;

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

        Response(struct pool &_caller_pool, WasInput *_body)
            :headers(_caller_pool), body(_body) {}

        /**
         * Are we currently receiving response metadata (such as
         * headers)?
         */
        bool IsReceivingMetadata() const {
            return receiving_metadata && !pending;
        }

        /**
         * Has the response been submitted to the response handler?
         */
        bool WasSubmitted() const {
            return !receiving_metadata;
        }
    } response;

    WasClient(struct pool &_pool, struct pool &_caller_pool,
              EventLoop &event_loop,
              Stopwatch *_stopwatch,
              int control_fd, int input_fd, int output_fd,
              WasLease &_lease,
              http_method_t method, Istream *body,
              HttpResponseHandler &_handler,
              CancellablePointer &cancel_ptr);

    void Destroy() {
        stopwatch_dump(stopwatch);
        pool_unref(&caller_pool);
        pool_unref(&pool);
    }

    /**
     * Cancel the request body by sending #WAS_COMMAND_PREMATURE to
     * the WAS child process.
     *
     * @return false on error (OnWasControlError() has been called).
     */
    bool CancelRequestBody() {
        if (request.body == nullptr)
            return true;

        uint64_t sent = was_output_free_p(&request.body);
        return control.SendUint64(WAS_COMMAND_PREMATURE, sent);
    }

    /**
     * Release the control channel and invoke WasLease::ReleaseWas().
     * If the control channel is clean (i.e. buffers are empty), it
     * will attempt to reuse the WAS child process.
     *
     * Prior to calling this method, the #WasInput and the #WasOutput
     * must be released already.
     */
    void ReleaseControl() {
        assert(request.body == nullptr);
        assert(response.body == nullptr || response.released);

        if (!control.IsDefined())
            /* already released */
            return;

        bool reuse = control.IsEmpty();
        control.ReleaseSocket();

        lease.ReleaseWas(reuse);
    }

    void ReleaseControlStop(uint64_t received) {
        assert(response.body == nullptr);

        if (!control.IsDefined())
            /* already released */
            return;

        request.ClearBody();

        if (!control.SendEmpty(WAS_COMMAND_STOP))
            return;

        control.ReleaseSocket();

        lease.ReleaseWasStop(received);
    }

    /**
     * Destroys the objects was_control, was_input, was_output and
     * releases the socket lease.
     */
    void Clear(GError *error) {
        request.ClearBody();

        if (response.body != nullptr)
            was_input_free_p(&response.body, error);
        else
            g_error_free(error);

        if (control.IsDefined())
            control.ReleaseSocket();

        lease.ReleaseWas(false);
    }

    /**
     * Like Clear(), but assumes the response body has not been
     * enabled.
     */
    void ClearUnused() {
        request.ClearBody();

        if (response.body != nullptr)
            was_input_free_unused_p(&response.body);

        if (control.IsDefined())
            control.ReleaseSocket();

        lease.ReleaseWas(false);
    }

    /**
     * Abort receiving the response status/headers from the WAS server.
     */
    void AbortResponseHeaders(GError *error) {
        assert(response.IsReceivingMetadata());

        ClearUnused();

        handler.InvokeError(error);
        Destroy();
    }

    /**
     * Abort receiving the response body from the WAS server.
     */
    void AbortResponseBody(GError *error) {
        assert(response.WasSubmitted());

        Clear(error);
        Destroy();
    }

    /**
     * Call this when end of the response body has been seen.  It will
     * take care of releasing the #WasClient.
     */
    void ResponseEof() {
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
    void AbortPending(GError *error) {
        assert(!response.IsReceivingMetadata() &&
               !response.WasSubmitted());

        Clear(error);
        Destroy();
    }

    /**
     * Abort receiving the response status/headers from the WAS server.
     */
    void AbortResponse(GError *error) {
        if (response.IsReceivingMetadata())
            AbortResponseHeaders(error);
        else if (response.WasSubmitted())
            AbortResponseBody(error);
        else
            AbortPending(error);
    }

    /**
     * Submit the pending response to our handler.
     *
     * @return false if our #WasControl instance has been disposed
     */
    bool SubmitPendingResponse();

    /* virtual methods from class Cancellable */
    void Cancel() override {
        /* Cancellable::Cancel() can only be used before the
           response was delivered to our callback */
        assert(!response.WasSubmitted());

        stopwatch_event(stopwatch, "cancel");

        if (response.body != nullptr)
            was_input_free_unused_p(&response.body);

        ReleaseControlStop(0);
        Destroy();
    }

    /* virtual methods from class WasControlHandler */
    bool OnWasControlPacket(enum was_command cmd,
                            ConstBuffer<void> payload) override;
    bool OnWasControlDrained() override;

    void OnWasControlDone() override {
        assert(request.body == nullptr);
        assert(response.body == nullptr);
        assert(!control.IsDefined());
    }

    void OnWasControlError(GError *error) override {
        assert(!control.IsDefined());

        stopwatch_event(stopwatch, "control_error");

        AbortResponse(error);
    }

    /* virtual methods from class WasOutputHandler */
    bool WasOutputLength(uint64_t length) override;
    bool WasOutputPremature(uint64_t length, GError *error) override;
    void WasOutputEof() override;
    void WasOutputError(GError *error) override;

    /* virtual methods from class WasInputHandler */
    void WasInputClose(uint64_t received) override;
    bool WasInputRelease() override;
    void WasInputEof() override;
    void WasInputError() override;
};

bool
WasClient::SubmitPendingResponse()
{
    assert(response.pending);
    assert(!response.WasSubmitted());

    stopwatch_event(stopwatch, "headers");

    response.pending = false;

    response.receiving_metadata = false;

    const ScopePoolRef ref(pool TRACE_ARGS);
    const ScopePoolRef caller_ref(caller_pool TRACE_ARGS);

    Istream *body;
    if (response.released) {
        was_input_free_unused_p(&response.body);
        body = istream_null_new(&caller_pool);

        ReleaseControl();
        Destroy();
    } else
        body = &was_input_enable(*response.body);

    handler.InvokeResponse(response.status, std::move(response.headers), body);
    return control.IsDefined();
}

/*
 * WasControlHandler
 */

bool
WasClient::OnWasControlPacket(enum was_command cmd, ConstBuffer<void> payload)
{
    GError *error;

    switch (cmd) {
        const uint32_t *status32_r;
        const uint16_t *status16_r;
        http_status_t status;
        const uint64_t *length_p;
        const char *p;

    case WAS_COMMAND_NOP:
        break;

    case WAS_COMMAND_REQUEST:
    case WAS_COMMAND_URI:
    case WAS_COMMAND_METHOD:
    case WAS_COMMAND_SCRIPT_NAME:
    case WAS_COMMAND_PATH_INFO:
    case WAS_COMMAND_QUERY_STRING:
    case WAS_COMMAND_PARAMETER:
        stopwatch_event(stopwatch, "control_error");
        error = g_error_new(was_quark(), 0,
                            "Unexpected WAS packet %d", cmd);
        AbortResponse(error);
        return false;

    case WAS_COMMAND_HEADER:
        if (!response.IsReceivingMetadata()) {
            stopwatch_event(stopwatch, "control_error");
            error = g_error_new_literal(was_quark(), 0,
                                        "response header was too late");
            AbortResponseBody(error);
            return false;
        }

        p = (const char *)memchr(payload.data, '=', payload.size);
        if (p == nullptr || p == payload.data) {
            stopwatch_event(stopwatch, "control_error");
            error = g_error_new_literal(was_quark(), 0,
                                        "Malformed WAS HEADER packet");
            AbortResponseHeaders(error);
            return false;
        }

        response.headers.Add(p_strndup_lower(&pool, (const char *)payload.data,
                                             p - (const char *)payload.data),
                             p_strndup(&pool, p + 1,
                                       (const char *)payload.data + payload.size - p - 1));
        break;

    case WAS_COMMAND_STATUS:
        if (!response.IsReceivingMetadata()) {
            stopwatch_event(stopwatch, "control_error");
            error = g_error_new_literal(was_quark(), 0,
                                "STATUS after body start");
            AbortResponseBody(error);
            return false;
        }

        status32_r = (const uint32_t *)payload.data;
        status16_r = (const uint16_t *)payload.data;

        if (payload.size == sizeof(*status32_r))
            status = (http_status_t)*status32_r;
        else if (payload.size == sizeof(*status16_r))
            status = (http_status_t)*status16_r;
        else {
            stopwatch_event(stopwatch, "control_error");
            error = g_error_new_literal(was_quark(), 0,
                                        "malformed STATUS");
            AbortResponseHeaders(error);
            return false;
        }

        if (!http_status_is_valid(status)) {
            stopwatch_event(stopwatch, "control_error");
            error = g_error_new_literal(was_quark(), 0,
                                        "malformed STATUS");
            AbortResponseHeaders(error);
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
            stopwatch_event(stopwatch, "control_error");
            error = g_error_new_literal(was_quark(), 0,
                                        "NO_DATA after body start");
            AbortResponseBody(error);
            return false;
        }

        response.receiving_metadata = false;

        if (response.body != nullptr)
            was_input_free_unused_p(&response.body);

        if (!CancelRequestBody())
            return false;

        ReleaseControl();

        handler.InvokeResponse(response.status, std::move(response.headers),
                               nullptr);

        Destroy();
        return false;

    case WAS_COMMAND_DATA:
        if (!response.IsReceivingMetadata()) {
            stopwatch_event(stopwatch, "control_error");
            error = g_error_new_literal(was_quark(), 0,
                                        "DATA after body start");
            AbortResponseBody(error);
            return false;
        }

        if (response.body == nullptr) {
            stopwatch_event(stopwatch, "control_error");
            error = g_error_new_literal(was_quark(), 0,
                                        "no response body allowed");
            AbortResponseHeaders(error);
            return false;
        }

        response.pending = true;
        break;

    case WAS_COMMAND_LENGTH:
        if (response.IsReceivingMetadata()) {
            stopwatch_event(stopwatch, "control_error");
            error = g_error_new_literal(was_quark(), 0,
                                        "LENGTH before DATA");
            AbortResponseHeaders(error);
            return false;
        }

        if (response.body == nullptr) {
            stopwatch_event(stopwatch, "control_error");
            error = g_error_new_literal(was_quark(), 0,
                                        "LENGTH after NO_DATA");
            AbortResponseBody(error);
            return false;
        }

        length_p = (const uint64_t *)payload.data;
        if (payload.size != sizeof(*length_p)) {
            stopwatch_event(stopwatch, "control_error");
            error = g_error_new_literal(was_quark(), 0,
                                        "malformed LENGTH packet");
            AbortResponseBody(error);
            return false;
        }

        if (!was_input_set_length(response.body, *length_p))
            return false;

        if (!control.IsDefined()) {
            /* through WasInputRelease(), the above
               was_input_set_length() call may have disposed the
               WasControl instance; this condition needs to be
               reported to our caller */

            if (response.pending)
                /* since OnWasControlDrained() isn't going to be
                   called (because we cancelled that), we need to do
                   this check manually */
                SubmitPendingResponse();

            return false;
        }

        break;

    case WAS_COMMAND_STOP:
        return CancelRequestBody();

    case WAS_COMMAND_PREMATURE:
        if (response.IsReceivingMetadata()) {
            stopwatch_event(stopwatch, "control_error");
            error = g_error_new_literal(was_quark(), 0,
                                        "PREMATURE before DATA");
            AbortResponseHeaders(error);
            return false;
        }

        length_p = (const uint64_t *)payload.data;
        if (payload.size != sizeof(*length_p)) {
            stopwatch_event(stopwatch, "control_error");
            error = g_error_new_literal(was_quark(), 0,
                                        "malformed PREMATURE packet");
            AbortResponseBody(error);
            return false;
        }

        if (response.body == nullptr)
            break;

        if (!was_input_premature(response.body, *length_p))
            return false;

        response.body = nullptr;
        ResponseEof();
        return false;
    }

    return true;
}

bool
WasClient::OnWasControlDrained()
{
    if (response.pending)
        return SubmitPendingResponse();
    else
        return true;
}

/*
 * Output handler
 */

bool
WasClient::WasOutputLength(uint64_t length)
{
    assert(control.IsDefined());
    assert(request.body != nullptr);

    return control.SendUint64(WAS_COMMAND_LENGTH, length);
}

bool
WasClient::WasOutputPremature(uint64_t length, GError *error)
{
    assert(control.IsDefined());
    assert(request.body != nullptr);

    stopwatch_event(stopwatch, "request_error");

    request.body = nullptr;

    /* XXX send PREMATURE, recover */
    (void)length;

    AbortResponse(error);
    return false;
}

void
WasClient::WasOutputEof()
{
    assert(request.body != nullptr);

    stopwatch_event(stopwatch, "request_eof");

    request.body = nullptr;
}

void
WasClient::WasOutputError(GError *error)
{
    assert(request.body != nullptr);

    stopwatch_event(stopwatch, "send_error");

    request.body = nullptr;

    AbortResponse(error);
}

/*
 * Input handler
 */

void
WasClient::WasInputClose(uint64_t received)
{
    assert(response.WasSubmitted());
    assert(response.body != nullptr);

    stopwatch_event(stopwatch, "close");

    response.body = nullptr;

    ReleaseControlStop(received);
    Destroy();
}

bool
WasClient::WasInputRelease()
{
    assert(response.body != nullptr);
    assert(!response.released);

    stopwatch_event(stopwatch, "eof");

    response.released = true;

    if (!CancelRequestBody())
        return false;

    ReleaseControl();
    return true;
}

void
WasClient::WasInputEof()
{
    assert(response.WasSubmitted());
    assert(response.body != nullptr);
    assert(response.released);

    response.body = nullptr;

    ResponseEof();
}

void
WasClient::WasInputError()
{
    assert(response.WasSubmitted());
    assert(response.body != nullptr);

    stopwatch_event(stopwatch, "error");

    response.body = nullptr;

    if (control.IsDefined())
        control.ReleaseSocket();

    lease.ReleaseWas(false);

    Destroy();
}

/*
 * constructor
 *
 */

inline
WasClient::WasClient(struct pool &_pool, struct pool &_caller_pool,
                     EventLoop &event_loop,
                     Stopwatch *_stopwatch,
                     int control_fd, int input_fd, int output_fd,
                     WasLease &_lease,
                     http_method_t method, Istream *body,
                     HttpResponseHandler &_handler,
                     CancellablePointer &cancel_ptr)
    :pool(_pool), caller_pool(_caller_pool),
     stopwatch(_stopwatch),
     lease(_lease),
     control(event_loop, control_fd, *this),
     handler(_handler),
     request(body != nullptr
             ? was_output_new(pool, event_loop, output_fd, *body, *this)
             : nullptr),
     response(_caller_pool,
              http_method_is_empty(method)
              ? nullptr
              : was_input_new(pool, event_loop, input_fd, *this))
{
    pool_ref(&caller_pool);

    cancel_ptr = *this;
}

static bool
SendRequest(WasControl &control,
            http_method_t method, const char *uri,
            const char *script_name, const char *path_info,
            const char *query_string,
            StringMap &headers, WasOutput *request_body,
            ConstBuffer<const char *> params)
{
    const uint32_t method32 = (uint32_t)method;

    return control.SendEmpty(WAS_COMMAND_REQUEST) &&
        (method == HTTP_METHOD_GET ||
         control.Send(WAS_COMMAND_METHOD, &method32, sizeof(method32))) &&
        control.SendString(WAS_COMMAND_URI, uri) &&
        (script_name == nullptr ||
         control.SendString(WAS_COMMAND_SCRIPT_NAME, script_name)) &&
        (path_info == nullptr ||
         control.SendString(WAS_COMMAND_PATH_INFO, path_info)) &&
        (query_string == nullptr ||
         control.SendString(WAS_COMMAND_QUERY_STRING, query_string)) &&
        control.SendStrmap(WAS_COMMAND_HEADER, headers) &&
        control.SendArray(WAS_COMMAND_PARAMETER, params) &&
        control.SendEmpty(request_body != nullptr
                          ? WAS_COMMAND_DATA
                          : WAS_COMMAND_NO_DATA) &&
        (request_body == nullptr || was_output_check_length(*request_body));
}

void
was_client_request(struct pool &caller_pool, EventLoop &event_loop,
                   Stopwatch *stopwatch,
                   int control_fd, int input_fd, int output_fd,
                   WasLease &lease,
                   http_method_t method, const char *uri,
                   const char *script_name, const char *path_info,
                   const char *query_string,
                   StringMap &headers, Istream *body,
                   ConstBuffer<const char *> params,
                   HttpResponseHandler &handler,
                   CancellablePointer &cancel_ptr)
{
    assert(http_method_is_valid(method));
    assert(uri != nullptr);

    struct pool *pool = pool_new_linear(&caller_pool, "was_client_request", 32768);
    auto client = NewFromPool<WasClient>(*pool, *pool, caller_pool,
                                         event_loop, stopwatch,
                                         control_fd, input_fd, output_fd,
                                         lease, method, body,
                                         handler, cancel_ptr);

    client->control.BulkOn();

    if (!SendRequest(client->control,
                     method, uri, script_name, path_info,
                     query_string, headers, client->request.body,
                     params))
        return;

    client->control.BulkOff();
}
