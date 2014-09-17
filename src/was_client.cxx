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
#include "pevent.h"
#include "direct.h"
#include "istream-internal.h"
#include "fd-util.h"
#include "strmap.hxx"
#include "pool.hxx"
#include "util/Cast.hxx"
#include "util/ConstBuffer.hxx"

#include <daemon/log.h>
#include <was/protocol.h>

#include <errno.h>
#include <string.h>
#include <sys/socket.h>

struct was_client {
    struct pool *pool, *caller_pool;

    struct was_control *control;

    struct lease_ref lease_ref;

    struct http_response_handler_ref handler;
    struct async_operation async;

    struct {
        struct was_output *body;
    } request;

    struct {
        http_status_t status;

        /**
         * Response headers being assembled.  This pointer is set to
         * nullptr before the response is dispatched to the response
         * handler.
         */
        struct strmap *headers;

        struct was_input *body;

        /**
         * If set, then the invocation of the response handler is
         * postponed, until the remaining control packets have been
         * evaluated.
         */
        bool pending;
    } response;
};

/**
 * Are we currently receiving response metadata (such as headers)?
 */
static bool
was_client_receiving_metadata(const struct was_client *client)
{
    return client->response.headers != nullptr && !client->response.pending;
}

/**
 * Has the response been submitted to the response handler?
 */
static bool
was_client_response_submitted(const struct was_client *client)
{
    return client->response.headers == nullptr;
}

static void
was_client_clear_request_body(struct was_client *client)
{
    if (client->request.body != nullptr)
        was_output_free_p(&client->request.body);
}

/**
 * Destroys the objects was_control, was_input, was_output and
 * releases the socket lease.
 */
static void
was_client_clear(struct was_client *client, GError *error)
{
    was_client_clear_request_body(client);

    if (client->response.body != nullptr)
        was_input_free_p(&client->response.body, error);
    else
        g_error_free(error);

    if (client->control != nullptr) {
        was_control_free(client->control);
        client->control = nullptr;
    }

    p_lease_release(client->lease_ref, false, *client->pool);
}

/**
 * Like was_client_clear(), but assumes the response body has not been
 * enabled.
 */
static void
was_client_clear_unused(struct was_client *client)
{
    was_client_clear_request_body(client);

    if (client->response.body != nullptr)
        was_input_free_unused_p(&client->response.body);

    if (client->control != nullptr) {
        was_control_free(client->control);
        client->control = nullptr;
    }

    p_lease_release(client->lease_ref, false, *client->pool);
}

/**
 * Abort receiving the response status/headers from the WAS server.
 */
static void
was_client_abort_response_headers(struct was_client *client, GError *error)
{
    assert(was_client_receiving_metadata(client));

    client->async.Finished();

    was_client_clear(client, g_error_copy(error));

    client->handler.InvokeAbort(error);
    pool_unref(client->caller_pool);
    pool_unref(client->pool);
}

/**
 * Abort receiving the response status/headers from the WAS server.
 */
static void
was_client_abort_response_body(struct was_client *client, GError *error)
{
    assert(was_client_response_submitted(client));

    was_client_clear(client, error);

    pool_unref(client->caller_pool);
    pool_unref(client->pool);
}

/**
 * Abort after
 */
static void
was_client_abort_response_empty(struct was_client *client)
{
    assert(was_client_response_submitted(client));

    was_client_clear_unused(client);

    pool_unref(client->caller_pool);
    pool_unref(client->pool);
}

/**
 * Call this when end of the response body has been seen.  It will
 * take care for releasing the #was_client.
 */
static void
was_client_response_eof(struct was_client *client)
{
    assert(was_client_response_submitted(client));
    assert(client->response.body == nullptr);

    if (client->request.body != nullptr ||
        !was_control_is_empty(client->control)) {
        was_client_abort_response_empty(client);
        return;
    }

    was_control_free(client->control);
    client->control = nullptr;

    p_lease_release(client->lease_ref, true, *client->pool);
    pool_unref(client->caller_pool);
    pool_unref(client->pool);
}

/**
 * Abort a pending response (BODY has been received, but the response
 * handler has not yet been invoked).
 */
static void
was_client_abort_pending(struct was_client *client, GError *error)
{
    assert(!was_client_receiving_metadata(client) &&
           !was_client_response_submitted(client));

    client->async.Finished();

    was_client_clear(client, error);

    pool_unref(client->caller_pool);
    pool_unref(client->pool);
}

/**
 * Abort receiving the response status/headers from the WAS server.
 */
static void
was_client_abort(struct was_client *client, GError *error)
{
    if (was_client_receiving_metadata(client))
        was_client_abort_response_headers(client, error);
    else if (was_client_response_submitted(client))
        was_client_abort_response_body(client, error);
    else
        was_client_abort_pending(client, error);
}


/*
 * Control channel handler
 */

static bool
was_client_control_packet(enum was_command cmd, const void *payload,
                          size_t payload_length, void *ctx)
{
    struct was_client *client = (struct was_client *)ctx;
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
        was_client_abort(client, error);
        return false;

    case WAS_COMMAND_HEADER:
        if (!was_client_receiving_metadata(client)) {
            error = g_error_new_literal(was_quark(), 0,
                                        "response header was too late");
            was_client_abort_response_body(client, error);
            return false;
        }

        p = (const char *)memchr(payload, '=', payload_length);
        if (p == nullptr || p == payload) {
            error = g_error_new_literal(was_quark(), 0,
                                        "Malformed WAS HEADER packet");
            was_client_abort_response_headers(client, error);
            return false;
        }

        client->response.headers->Add(p_strndup(client->pool, (const char *)payload,
                                                p - (const char *)payload),
                                      p_strndup(client->pool, p + 1,
                                                (const char*)payload + payload_length - p - 1));
        break;

    case WAS_COMMAND_STATUS:
        if (!was_client_receiving_metadata(client)) {
            error = g_error_new_literal(was_quark(), 0,
                                "STATUS after body start");
            was_client_abort_response_body(client, error);
            return false;
        }

        status_r = (const uint32_t *)payload;
        if (payload_length != sizeof(*status_r) ||
            !http_status_is_valid((http_status_t)*status_r)) {
            error = g_error_new_literal(was_quark(), 0,
                                        "malformed STATUS");
            was_client_abort_response_body(client, error);
            return false;
        }

        client->response.status = (http_status_t)*status_r;

        if (http_status_is_empty(client->response.status) &&
            client->response.body != nullptr)
            /* no response body possible with this status; release the
               object */
            was_input_free_unused_p(&client->response.body);

        break;

    case WAS_COMMAND_NO_DATA:
        if (!was_client_receiving_metadata(client)) {
            error = g_error_new_literal(was_quark(), 0,
                                        "NO_DATA after body start");
            was_client_abort_response_body(client, error);
            return false;
        }

        headers = client->response.headers;
        client->response.headers = nullptr;

        if (client->response.body != nullptr) {
            const ScopePoolRef ref(*client->pool TRACE_ARGS);
            was_input_free_unused_p(&client->response.body);

            if (client->control == nullptr)
                /* aborted; don't invoke response handler */
                return false;
        }

        client->async.Finished();

        was_client_clear_request_body(client);

        client->handler.InvokeResponse(client->response.status,
                                       headers, nullptr);
        was_client_response_eof(client);
        return false;

    case WAS_COMMAND_DATA:
        if (!was_client_receiving_metadata(client)) {
            error = g_error_new_literal(was_quark(), 0,
                                        "DATA after body start");
            was_client_abort_response_body(client, error);
            return false;
        }

        if (client->response.body == nullptr) {
            error = g_error_new_literal(was_quark(), 0,
                                        "no response body allowed");
            was_client_abort_response_headers(client, error);
            return false;
        }

        client->response.pending = true;
        break;

    case WAS_COMMAND_LENGTH:
        if (was_client_receiving_metadata(client)) {
            error = g_error_new_literal(was_quark(), 0,
                                        "LENGTH before DATA");
            was_client_abort_response_headers(client, error);
            return false;
        }

        if (client->response.body == nullptr) {
            error = g_error_new_literal(was_quark(), 0,
                                        "LENGTH after NO_DATA");
            was_client_abort_response_body(client, error);
            return false;
        }

        length_p = (const uint64_t *)payload;
        if (payload_length != sizeof(*length_p)) {
            error = g_error_new_literal(was_quark(), 0,
                                        "malformed LENGTH packet");
            was_client_abort_response_body(client, error);
            return false;
        }

        if (!was_input_set_length(client->response.body, *length_p))
            return false;

        break;

    case WAS_COMMAND_STOP:
        if (client->request.body != nullptr) {
            uint64_t sent = was_output_free_p(&client->request.body);
            return was_control_send_uint64(client->control,
                                           WAS_COMMAND_PREMATURE, sent);
        }

        break;

    case WAS_COMMAND_PREMATURE:
        if (was_client_receiving_metadata(client)) {
            error = g_error_new_literal(was_quark(), 0,
                                        "PREMATURE before DATA");
            was_client_abort_response_headers(client, error);
            return false;
        }

        length_p = (const uint64_t *)payload;
        if (payload_length != sizeof(*length_p)) {
            error = g_error_new_literal(was_quark(), 0,
                                        "malformed PREMATURE packet");
            was_client_abort_response_body(client, error);
            return false;
        }

        if (client->response.body == nullptr)
            break;

        if (!was_input_premature(client->response.body, *length_p))
            return false;

        return false;
    }

    return true;
}

static bool
was_client_control_drained(void *ctx)
{
    struct was_client *client = (struct was_client *)ctx;

    if (!client->response.pending)
        return true;

    assert(!was_client_response_submitted(client));

    client->response.pending = false;

    struct strmap *headers = client->response.headers;
    client->response.headers = nullptr;

    struct istream *body = was_input_enable(client->response.body);

    client->async.Finished();

    const ScopePoolRef ref(*client->pool TRACE_ARGS);
    const ScopePoolRef caller_ref(*client->caller_pool TRACE_ARGS);

    client->handler.InvokeResponse(client->response.status, headers, body);
    if (client->control == nullptr)
        /* closed, must return false */
        return false;

    return true;
}

static void
was_client_control_eof(void *ctx)
{
    struct was_client *client = (struct was_client *)ctx;

    assert(client->request.body == nullptr);
    assert(client->response.body == nullptr);

    client->control = nullptr;
}

static void
was_client_control_abort(GError *error, void *ctx)
{
    struct was_client *client = (struct was_client *)ctx;

    client->control = nullptr;

    was_client_abort(client, error);
}

static const struct was_control_handler was_client_control_handler = {
    .packet = was_client_control_packet,
    .drained = was_client_control_drained,
    .eof = was_client_control_eof,
    .abort = was_client_control_abort,
};

/*
 * Output handler
 */

static bool
was_client_output_length(uint64_t length, void *ctx)
{
    struct was_client *client = (struct was_client *)ctx;

    assert(client->control != nullptr);
    assert(client->request.body != nullptr);

    return was_control_send_uint64(client->control,
                                   WAS_COMMAND_LENGTH, length);
}

static bool
was_client_output_premature(uint64_t length, GError *error, void *ctx)
{
    struct was_client *client = (struct was_client *)ctx;

    assert(client->control != nullptr);
    assert(client->request.body != nullptr);

    client->request.body = nullptr;

    /* XXX send PREMATURE, recover */
    (void)length;

    was_client_abort(client, error);
    return false;
}

static void
was_client_output_eof(void *ctx)
{
    struct was_client *client = (struct was_client *)ctx;

    assert(client->request.body != nullptr);

    client->request.body = nullptr;
}

static void
was_client_output_abort(GError *error, void *ctx)
{
    struct was_client *client = (struct was_client *)ctx;

    assert(client->request.body != nullptr);

    client->request.body = nullptr;

    was_client_abort(client, error);
}

static const struct was_output_handler was_client_output_handler = {
    .length = was_client_output_length,
    .premature = was_client_output_premature,
    .eof = was_client_output_eof,
    .abort = was_client_output_abort,
};


/*
 * Input handler
 */

static void
was_client_input_eof(void *ctx)
{
    struct was_client *client = (struct was_client *)ctx;

    assert(was_client_response_submitted(client) || client->response.pending);
    assert(client->response.body != nullptr);

    client->response.body = nullptr;

    if (client->response.pending) {
        struct strmap *headers = client->response.headers;

        /* LENGTH=0 received, therefore was_input has been closed, and
           we use an istream_null instead */
        struct istream *body = istream_null_new(client->caller_pool);

        client->async.Finished();

        client->handler.InvokeResponse(client->response.status, headers, body);

        if (client->request.body == nullptr) {
            /* reuse the connection */
            was_control_free(client->control);
            p_lease_release(client->lease_ref, true, *client->pool);
            pool_unref(client->caller_pool);
            pool_unref(client->pool);
        } else
            was_client_abort_response_empty(client);
        return;
    }

    was_client_response_eof(client);
}

static void
was_client_input_abort(void *ctx)
{
    struct was_client *client = (struct was_client *)ctx;

    assert(was_client_response_submitted(client));
    assert(client->response.body != nullptr);

    client->response.body = nullptr;

    was_client_abort_response_empty(client);
}

static const struct was_input_handler was_client_input_handler = {
    .eof = was_client_input_eof,
    .premature = was_client_input_abort, // XXX implement
    .abort = was_client_input_abort,
};


/*
 * async operation
 *
 */

static struct was_client *
async_to_was_client(struct async_operation *ao)
{
    return &ContainerCast2(*ao, &was_client::async);
}

static void
was_client_request_abort(struct async_operation *ao)
{
    struct was_client *client = async_to_was_client(ao);

    /* async_operation_ref::Abort() can only be used before the
       response was delivered to our callback */
    assert(!was_client_response_submitted(client));

    was_client_clear_unused(client);

    pool_unref(client->caller_pool);
    pool_unref(client->pool);
}

static const struct async_operation_class was_client_async_operation = {
    .abort = was_client_request_abort,
};


/*
 * constructor
 *
 */

void
was_client_request(struct pool *caller_pool, int control_fd,
                   int input_fd, int output_fd,
                   const struct lease *lease, void *lease_ctx,
                   http_method_t method, const char *uri,
                   const char *script_name, const char *path_info,
                   const char *query_string,
                   struct strmap *headers, struct istream *body,
                   ConstBuffer<const char *> params,
                   const struct http_response_handler *handler,
                   void *handler_ctx,
                   struct async_operation_ref *async_ref)
{
    assert(http_method_is_valid(method));
    assert(uri != nullptr);

    struct pool *pool = pool_new_linear(caller_pool, "was_client_request", 32768);
    auto client = NewFromPool<struct was_client>(*pool);
    client->pool = pool;
    pool_ref(caller_pool);
    client->caller_pool = caller_pool;

    client->control = was_control_new(pool, control_fd,
                                      &was_client_control_handler, client);

    p_lease_ref_set(client->lease_ref, *lease, lease_ctx,
                    *pool, "was_client_lease");

    client->handler.Set(*handler, handler_ctx);

    client->async.Init(was_client_async_operation);
    async_ref->Set(client->async);

    client->request.body = body != nullptr
        ? was_output_new(pool, output_fd, body,
                         &was_client_output_handler, client)
        : nullptr;

    client->response.status = HTTP_STATUS_OK;
    client->response.headers = strmap_new(caller_pool);
    client->response.body = !http_method_is_empty(method)
        ? was_input_new(pool, input_fd, &was_client_input_handler, client)
        : nullptr;
    client->response.pending = false;

    uint32_t method32 = (uint32_t)method;

    was_control_bulk_on(client->control);

    if (!was_control_send_empty(client->control, WAS_COMMAND_REQUEST) ||
        (method != HTTP_METHOD_GET &&
         !was_control_send(client->control, WAS_COMMAND_METHOD,
                           &method32, sizeof(method32))) ||
        !was_control_send_string(client->control, WAS_COMMAND_URI, uri))
        return;

    if ((script_name != nullptr &&
         !was_control_send_string(client->control, WAS_COMMAND_SCRIPT_NAME,
                                  script_name)) ||
        (path_info != nullptr &&
         !was_control_send_string(client->control, WAS_COMMAND_PATH_INFO,
                                  path_info)) ||
        (query_string != nullptr &&
         !was_control_send_string(client->control, WAS_COMMAND_QUERY_STRING,
                                  query_string)) ||
        (headers != nullptr &&
         !was_control_send_strmap(client->control, WAS_COMMAND_HEADER,
                                  headers)) ||
        !was_control_send_array(client->control, WAS_COMMAND_PARAMETER,
                                params) ||
        !was_control_send_empty(client->control,
                                client->request.body != nullptr
                                ? WAS_COMMAND_DATA : WAS_COMMAND_NO_DATA)) {
        GError *error = g_error_new_literal(was_quark(), 0,
                                            "Failed to send WAS request");
        was_client_abort_response_headers(client, error);
        return;
    }

    was_control_bulk_off(client->control);
}
