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
#include "http_response.hxx"
#include "async.hxx"
#include "please.hxx"
#include "direct.hxx"
#include "istream/istream_null.hxx"
#include "strmap.hxx"
#include "pool.hxx"
#include "util/Cast.hxx"
#include "util/ConstBuffer.hxx"

#include <daemon/log.h>
#include <was/protocol.h>

#include <errno.h>
#include <string.h>
#include <sys/socket.h>

struct WasClient final : WasControlHandler, WasOutputHandler, WasInputHandler {
    struct pool *pool, *caller_pool;

    WasControl *control;

    struct lease_ref lease_ref;

    struct http_response_handler_ref handler;
    struct async_operation operation;

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
         * Response headers being assembled.  This pointer is set to
         * nullptr before the response is dispatched to the response
         * handler.
         */
        struct strmap *headers;

        WasInput *body;

        /**
         * If set, then the invocation of the response handler is
         * postponed, until the remaining control packets have been
         * evaluated.
         */
        bool pending = false;

        Response(struct pool &_caller_pool, WasInput *_body)
            :headers(strmap_new(&_caller_pool)), body(_body) {}

        /**
         * Are we currently receiving response metadata (such as
         * headers)?
         */
        bool IsReceivingMetadata() const {
            return headers != nullptr && !pending;
        }

        /**
         * Has the response been submitted to the response handler?
         */
        bool WasSubmitted() const {
            return headers == nullptr;
        }
    } response;

    WasClient(struct pool &_pool, struct pool &_caller_pool,
              int control_fd, int input_fd, int output_fd,
              Lease &lease,
              http_method_t method, Istream *body,
              const struct http_response_handler &handler,
              void *handler_ctx,
              struct async_operation_ref &async_ref);

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

        if (control != nullptr) {
            was_control_free(control);
            control = nullptr;
        }

        p_lease_release(lease_ref, false, *pool);
    }

    /**
     * Like Clear(), but assumes the response body has not been
     * enabled.
     */
    void ClearUnused() {
        request.ClearBody();

        if (response.body != nullptr)
            was_input_free_unused_p(&response.body);

        if (control != nullptr) {
            was_control_free(control);
            control = nullptr;
        }

        p_lease_release(lease_ref, false, *pool);
    }

    /**
     * Abort receiving the response status/headers from the WAS server.
     */
    void AbortResponseHeaders(GError *error) {
        assert(response.IsReceivingMetadata());

        operation.Finished();

        ClearUnused();

        handler.InvokeAbort(error);
        pool_unref(caller_pool);
        pool_unref(pool);
    }

    /**
     * Abort receiving the response body from the WAS server.
     */
    void AbortResponseBody(GError *error) {
        assert(response.WasSubmitted());

        Clear(error);

        pool_unref(caller_pool);
        pool_unref(pool);
    }

    /**
     * Abort after
     */
    void AbortResponseEmpty() {
        assert(response.WasSubmitted());

        ClearUnused();

        pool_unref(caller_pool);
        pool_unref(pool);
    }

    /**
     * Call this when end of the response body has been seen.  It will
     * take care of releasing the #WasClient.
     */
    void ResponseEof() {
        assert(response.WasSubmitted());
        assert(response.body == nullptr);

        if (request.body != nullptr ||
            !was_control_is_empty(control)) {
            AbortResponseEmpty();
            return;
        }

        was_control_free(control);
        control = nullptr;

        p_lease_release(lease_ref, true, *pool);
        pool_unref(caller_pool);
        pool_unref(pool);
    }

    /**
     * Abort a pending response (BODY has been received, but the response
     * handler has not yet been invoked).
     */
    void AbortPending(GError *error) {
        assert(!response.IsReceivingMetadata() &&
               !response.WasSubmitted());

        operation.Finished();

        Clear(error);

        pool_unref(caller_pool);
        pool_unref(pool);
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

    void Abort() {
        /* async_operation_ref::Abort() can only be used before the
           response was delivered to our callback */
        assert(!response.WasSubmitted());

        ClearUnused();

        pool_unref(caller_pool);
        pool_unref(pool);
    }

    /* virtual methods from class WasControlHandler */
    bool OnWasControlPacket(enum was_command cmd,
                            ConstBuffer<void> payload) override;
    bool OnWasControlDrained() override;

    void OnWasControlDone() override {
        assert(request.body == nullptr);
        assert(response.body == nullptr);

        control = nullptr;
    }

    void OnWasControlError(GError *error) override {
        control = nullptr;
        AbortResponse(error);
    }

    /* virtual methods from class WasOutputHandler */
    bool WasOutputLength(uint64_t length) override;
    bool WasOutputPremature(uint64_t length, GError *error) override;
    void WasOutputEof() override;
    void WasOutputError(GError *error) override;

    /* virtual methods from class WasInputHandler */
    void WasInputClose() override;
    void WasInputRelease() override;
    void WasInputEof() override;
    void WasInputError() override;
};


/*
 * WasControlHandler
 */

bool
WasClient::OnWasControlPacket(enum was_command cmd, ConstBuffer<void> payload)
{
    GError *error;

    switch (cmd) {
        const uint32_t *status_r;
        struct strmap *headers;
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
        error = g_error_new(was_quark(), 0,
                            "Unexpected WAS packet %d", cmd);
        AbortResponse(error);
        return false;

    case WAS_COMMAND_HEADER:
        if (!response.IsReceivingMetadata()) {
            error = g_error_new_literal(was_quark(), 0,
                                        "response header was too late");
            AbortResponseBody(error);
            return false;
        }

        p = (const char *)memchr(payload.data, '=', payload.size);
        if (p == nullptr || p == payload.data) {
            error = g_error_new_literal(was_quark(), 0,
                                        "Malformed WAS HEADER packet");
            AbortResponseHeaders(error);
            return false;
        }

        response.headers->Add(p_strndup_lower(pool, (const char *)payload.data,
                                              p - (const char *)payload.data),
                              p_strndup(pool, p + 1,
                                        (const char *)payload.data + payload.size - p - 1));
        break;

    case WAS_COMMAND_STATUS:
        if (!response.IsReceivingMetadata()) {
            error = g_error_new_literal(was_quark(), 0,
                                "STATUS after body start");
            AbortResponseBody(error);
            return false;
        }

        status_r = (const uint32_t *)payload.data;
        if (payload.size != sizeof(*status_r) ||
            !http_status_is_valid((http_status_t)*status_r)) {
            error = g_error_new_literal(was_quark(), 0,
                                        "malformed STATUS");
            AbortResponseBody(error);
            return false;
        }

        response.status = (http_status_t)*status_r;

        if (http_status_is_empty(response.status) &&
            response.body != nullptr)
            /* no response body possible with this status; release the
               object */
            was_input_free_unused_p(&response.body);

        break;

    case WAS_COMMAND_NO_DATA:
        if (!response.IsReceivingMetadata()) {
            error = g_error_new_literal(was_quark(), 0,
                                        "NO_DATA after body start");
            AbortResponseBody(error);
            return false;
        }

        headers = response.headers;
        response.headers = nullptr;

        if (response.body != nullptr) {
            const ScopePoolRef ref(*pool TRACE_ARGS);
            was_input_free_unused_p(&response.body);

            if (control == nullptr)
                /* aborted; don't invoke response handler */
                return false;
        }

        operation.Finished();

        request.ClearBody();

        handler.InvokeResponse(response.status, headers, nullptr);
        ResponseEof();
        return false;

    case WAS_COMMAND_DATA:
        if (!response.IsReceivingMetadata()) {
            error = g_error_new_literal(was_quark(), 0,
                                        "DATA after body start");
            AbortResponseBody(error);
            return false;
        }

        if (response.body == nullptr) {
            error = g_error_new_literal(was_quark(), 0,
                                        "no response body allowed");
            AbortResponseHeaders(error);
            return false;
        }

        response.pending = true;
        break;

    case WAS_COMMAND_LENGTH:
        if (response.IsReceivingMetadata()) {
            error = g_error_new_literal(was_quark(), 0,
                                        "LENGTH before DATA");
            AbortResponseHeaders(error);
            return false;
        }

        if (response.body == nullptr) {
            error = g_error_new_literal(was_quark(), 0,
                                        "LENGTH after NO_DATA");
            AbortResponseBody(error);
            return false;
        }

        length_p = (const uint64_t *)payload.data;
        if (payload.size != sizeof(*length_p)) {
            error = g_error_new_literal(was_quark(), 0,
                                        "malformed LENGTH packet");
            AbortResponseBody(error);
            return false;
        }

        if (!was_input_set_length(response.body, *length_p))
            return false;

        break;

    case WAS_COMMAND_STOP:
        if (request.body != nullptr) {
            uint64_t sent = was_output_free_p(&request.body);
            return was_control_send_uint64(control,
                                           WAS_COMMAND_PREMATURE, sent);
        }

        break;

    case WAS_COMMAND_PREMATURE:
        if (response.IsReceivingMetadata()) {
            error = g_error_new_literal(was_quark(), 0,
                                        "PREMATURE before DATA");
            AbortResponseHeaders(error);
            return false;
        }

        length_p = (const uint64_t *)payload.data;
        if (payload.size != sizeof(*length_p)) {
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
    if (!response.pending)
        return true;

    assert(!response.WasSubmitted());

    response.pending = false;

    struct strmap *headers = response.headers;
    response.headers = nullptr;

    Istream *body = &was_input_enable(*response.body);

    operation.Finished();

    const ScopePoolRef ref(*pool TRACE_ARGS);
    const ScopePoolRef caller_ref(*caller_pool TRACE_ARGS);

    handler.InvokeResponse(response.status, headers, body);
    if (control == nullptr)
        /* closed, must return false */
        return false;

    return true;
}

/*
 * Output handler
 */

bool
WasClient::WasOutputLength(uint64_t length)
{
    assert(control != nullptr);
    assert(request.body != nullptr);

    return was_control_send_uint64(control, WAS_COMMAND_LENGTH, length);
}

bool
WasClient::WasOutputPremature(uint64_t length, GError *error)
{
    assert(control != nullptr);
    assert(request.body != nullptr);

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
    request.body = nullptr;
}

void
WasClient::WasOutputError(GError *error)
{
    assert(request.body != nullptr);
    request.body = nullptr;

    AbortResponse(error);
}

/*
 * Input handler
 */

void
WasClient::WasInputClose()
{
    // TODO: implement with STOP
    WasInputError();
}

void
WasClient::WasInputRelease()
{
}

void
WasClient::WasInputEof()
{
    assert(response.WasSubmitted() || response.pending);
    assert(response.body != nullptr);

    response.body = nullptr;

    if (response.pending) {
        struct strmap *headers = response.headers;

        /* LENGTH=0 received, therefore WasInput has been closed, and
           we use an istream_null instead */
        Istream *body = istream_null_new(caller_pool);

        operation.Finished();

        handler.InvokeResponse(response.status, headers, body);

        if (request.body == nullptr) {
            /* reuse the connection */
            was_control_free(control);
            p_lease_release(lease_ref, true, *pool);
            pool_unref(caller_pool);
            pool_unref(pool);
        } else
            AbortResponseEmpty();
        return;
    }

    ResponseEof();
}

void
WasClient::WasInputError()
{
    assert(response.WasSubmitted());
    assert(response.body != nullptr);

    response.body = nullptr;

    AbortResponseEmpty();
}

/*
 * constructor
 *
 */

inline
WasClient::WasClient(struct pool &_pool, struct pool &_caller_pool,
                     int control_fd, int input_fd, int output_fd,
                     Lease &lease,
                     http_method_t method, Istream *body,
                     const struct http_response_handler &_handler,
                     void *handler_ctx,
                     struct async_operation_ref &async_ref)
    :pool(&_pool), caller_pool(&_caller_pool),
     control(was_control_new(pool, control_fd, *this)),
     request(body != nullptr
             ? was_output_new(*pool, output_fd, *body, *this)
             : nullptr),
     response(_caller_pool,
              http_method_is_empty(method)
              ? nullptr
              : was_input_new(pool, input_fd, *this))
{
    pool_ref(caller_pool);

    p_lease_ref_set(lease_ref, lease, *pool, "was_client_lease");

    handler.Set(_handler, handler_ctx);

    operation.Init2<WasClient>();
    async_ref.Set(operation);
}

static bool
SendRequest(WasControl &control,
            http_method_t method, const char *uri,
            const char *script_name, const char *path_info,
            const char *query_string,
            struct strmap *headers, WasOutput *request_body,
            ConstBuffer<const char *> params)
{
    const uint32_t method32 = (uint32_t)method;

    return was_control_send_empty(&control, WAS_COMMAND_REQUEST) &&
        (method == HTTP_METHOD_GET ||
         was_control_send(&control, WAS_COMMAND_METHOD,
                          &method32, sizeof(method32))) &&
        was_control_send_string(&control, WAS_COMMAND_URI, uri) &&
        (script_name == nullptr ||
         was_control_send_string(&control, WAS_COMMAND_SCRIPT_NAME,
                                 script_name)) &&
        (path_info == nullptr ||
         was_control_send_string(&control, WAS_COMMAND_PATH_INFO,
                                 path_info)) &&
        (query_string == nullptr ||
         was_control_send_string(&control, WAS_COMMAND_QUERY_STRING,
                                 query_string)) &&
        (headers == nullptr ||
         was_control_send_strmap(&control, WAS_COMMAND_HEADER, headers)) &&
        was_control_send_array(&control, WAS_COMMAND_PARAMETER, params) &&
        was_control_send_empty(&control,
                               request_body != nullptr
                               ? WAS_COMMAND_DATA : WAS_COMMAND_NO_DATA) &&
        (request_body == nullptr || was_output_check_length(*request_body));
}

void
was_client_request(struct pool *caller_pool, int control_fd,
                   int input_fd, int output_fd,
                   Lease &lease,
                   http_method_t method, const char *uri,
                   const char *script_name, const char *path_info,
                   const char *query_string,
                   struct strmap *headers, Istream *body,
                   ConstBuffer<const char *> params,
                   const struct http_response_handler *handler,
                   void *handler_ctx,
                   struct async_operation_ref *async_ref)
{
    assert(http_method_is_valid(method));
    assert(uri != nullptr);

    struct pool *pool = pool_new_linear(caller_pool, "was_client_request", 32768);
    auto client = NewFromPool<WasClient>(*pool, *pool, *caller_pool,
                                         control_fd, input_fd, output_fd,
                                         lease, method, body,
                                         *handler, handler_ctx, *async_ref);

    was_control_bulk_on(client->control);

    if (!SendRequest(*client->control,
                     method, uri, script_name, path_info,
                     query_string, headers, client->request.body,
                     params)) {
        GError *error = g_error_new_literal(was_quark(), 0,
                                            "Failed to send WAS request");
        client->AbortResponseHeaders(error);
        return;
    }

    was_control_bulk_off(client->control);
}
