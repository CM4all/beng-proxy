/*
 * Web Application Socket client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "was-client.h"
#include "was-control.h"
#include "was-output.h"
#include "was-input.h"
#include "http-response.h"
#include "async.h"
#include "please.h"
#include "pevent.h"
#include "direct.h"
#include "istream-internal.h"
#include "fifo-buffer.h"
#include "buffered-io.h"
#include "fd-util.h"

#include <daemon/log.h>
#include <was/protocol.h>

#include <errno.h>
#include <string.h>
#include <sys/socket.h>

struct was_client {
    pool_t pool, caller_pool;

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
         * NULL before the response is dispatched to the response
         * handler.
         */
        struct strmap *headers;

        struct was_input *body;
    } response;
};

/**
 * Are we currently receiving response metadata (such as headers)?
 */
static bool
was_client_receiving_metadata(const struct was_client *client)
{
    return client->response.headers != NULL;
}

/**
 * Has the response been submitted to the response handler?
 */
static bool
was_client_response_submitted(const struct was_client *client)
{
    return client->response.headers == NULL;
}

/**
 * Abort receiving the response status/headers from the WAS server.
 */
static void
was_client_abort_response_headers(struct was_client *client)
{
    assert(was_client_receiving_metadata(client));

    async_operation_finished(&client->async);

    if (client->request.body != NULL)
        was_output_free_p(&client->request.body);

    if (client->response.body != NULL)
        was_input_free_p(&client->response.body);

    if (client->control != NULL) {
        was_control_free(client->control);
        client->control = NULL;
    }

    p_lease_release(&client->lease_ref, false, client->pool);

    http_response_handler_invoke_abort(&client->handler);
    pool_unref(client->caller_pool);
    pool_unref(client->pool);
}

/**
 * Abort receiving the response status/headers from the WAS server.
 */
static void
was_client_abort_response_body(struct was_client *client)
{
    assert(was_client_response_submitted(client));

    if (client->request.body != NULL)
        was_output_free_p(&client->request.body);

    if (client->response.body != NULL)
        was_input_free_p(&client->response.body);

    if (client->control != NULL) {
        was_control_free(client->control);
        client->control = NULL;
    }

    p_lease_release(&client->lease_ref, false, client->pool);
    pool_unref(client->caller_pool);
    pool_unref(client->pool);
}

/**
 * Abort receiving the response status/headers from the WAS server.
 */
static void
was_client_abort(struct was_client *client)
{
    if (was_client_receiving_metadata(client))
        was_client_abort_response_headers(client);
    else
        was_client_abort_response_body(client);
}


/*
 * Control channel handler
 */

static bool
was_client_control_packet(enum was_command cmd, const void *payload,
                          size_t payload_length, void *ctx)
{
    struct was_client *client = ctx;

    switch (cmd) {
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
        was_client_abort(client);
        return false;

    case WAS_COMMAND_HEADER:
        if (!was_client_receiving_metadata(client)) {
            daemon_log(2, "was-client: response header was too late\n");
            was_client_abort_response_body(client);
            return false;
        }

        p = memchr(payload, '=', payload_length);
        if (p == NULL || p == payload) {
            was_client_abort_response_headers(client);
            return false;
        }

        strmap_add(client->response.headers,
                   p_strndup(client->pool, payload, p - (const char*)payload),
                   p_strndup(client->pool, p + 1,
                             (const char*)payload + payload_length - p));
        break;

    case WAS_COMMAND_STATUS:
        if (!was_client_receiving_metadata(client)) {
            daemon_log(2, "was-client: STATUS after body start\n");
            was_client_abort_response_body(client);
            return false;
        }

        const uint32_t *status_r = payload;
        if (payload_length != sizeof(*status_r) ||
            !http_status_is_valid((http_status_t)*status_r)) {
            daemon_log(2, "was-client: malformed STATUS\n");
            was_client_abort_response_body(client);
            return false;
        }

        client->response.status = (http_status_t)*status_r;

        if (http_status_is_empty(client->response.status) &&
            client->response.body != NULL)
            /* no response body possible with this status; release the
               object */
            was_input_free_p(&client->response.body);

        break;

    case WAS_COMMAND_NO_DATA:
        if (!was_client_receiving_metadata(client)) {
            daemon_log(2, "was-client: NO_DATA after body start\n");
            was_client_abort_response_body(client);
            return false;
        }

        headers = client->response.headers;
        client->response.headers = NULL;

        if (client->response.body != NULL) {
            pool_ref(client->pool);
            was_input_free_p(&client->response.body);

            if (client->control == NULL) {
                /* aborted; don't invoke response handler */
                pool_unref(client->pool);
                return false;
            }

            pool_unref(client->pool);
        }

        async_operation_finished(&client->async);

        http_response_handler_invoke_response(&client->handler,
                                              client->response.status,
                                              headers, NULL);
        was_client_abort_response_body(client);
        return false;

    case WAS_COMMAND_DATA:
        if (!was_client_receiving_metadata(client)) {
            daemon_log(2, "was-client: DATA after body start\n");
            was_client_abort_response_body(client);
            return false;
        }

        if (client->response.body == NULL) {
            daemon_log(2, "was-client: no response body allowed\n");
            was_client_abort_response_headers(client);
            return false;
        }

        headers = client->response.headers;
        client->response.headers = NULL;

        istream_t body = was_input_enable(client->response.body);

        async_operation_finished(&client->async);

        pool_ref(client->pool);
        http_response_handler_invoke_response(&client->handler,
                                              client->response.status,
                                              headers, body);
        if (client->control == NULL) {
            /* closed, must return false */
            pool_unref(client->pool);
            return false;
        }

        pool_unref(client->pool);
        break;

    case WAS_COMMAND_LENGTH:
        if (was_client_receiving_metadata(client)) {
            daemon_log(2, "was-client: LENGTH before DATA\n");
            was_client_abort_response_headers(client);
            return false;
        }

        if (client->response.body == NULL) {
            daemon_log(2, "was-client: LENGTH after NO_DATA\n");
            was_client_abort_response_body(client);
            return false;
        }

        length_p = payload;
        if (payload_length != sizeof(*length_p)) {
            was_client_abort_response_body(client);
            return false;
        }

        if (!was_input_set_length(client->response.body, *length_p))
            return false;

        break;

    case WAS_COMMAND_STOP:
        if (client->request.body != NULL) {
            uint64_t sent = was_output_free_p(&client->request.body);
            return was_control_send_uint64(client->control,
                                           WAS_COMMAND_PREMATURE, sent);
        }

        break;

    case WAS_COMMAND_PREMATURE:
        if (was_client_receiving_metadata(client)) {
            daemon_log(2, "was-client: PREMATURE before DATA\n");
            was_client_abort_response_headers(client);
            return false;
        }

        length_p = payload;
        if (payload_length != sizeof(*length_p)) {
            was_client_abort_response_body(client);
            return false;
        }

        if (client->response.body == NULL)
            break;

        if (!was_input_premature(client->response.body, *length_p))
            return false;

        return false;
    }

    return true;
}

static void
was_client_control_eof(void *ctx)
{
    struct was_client *client = ctx;

    assert(client->request.body == NULL);
    assert(client->response.body == NULL);

    client->control = NULL;
}

static void
was_client_control_abort(void *ctx)
{
    struct was_client *client = ctx;

    client->control = NULL;

    was_client_abort(client);
}

static const struct was_control_handler was_client_control_handler = {
    .packet = was_client_control_packet,
    .eof = was_client_control_eof,
    .abort = was_client_control_abort,
};

/*
 * Output handler
 */

static bool
was_client_output_length(uint64_t length, void *ctx)
{
    struct was_client *client = ctx;

    assert(client->control != NULL);
    assert(client->request.body != NULL);

    return was_control_send_uint64(client->control,
                                   WAS_COMMAND_LENGTH, length);
}

static bool
was_client_output_premature(uint64_t length, void *ctx)
{
    struct was_client *client = ctx;

    assert(client->control != NULL);
    assert(client->request.body != NULL);

    client->request.body = NULL;

    /* XXX send PREMATURE, recover */
    (void)length;
    was_client_abort(client);
    return false;
}

static void
was_client_output_eof(void *ctx)
{
    struct was_client *client = ctx;

    assert(client->request.body != NULL);

    client->request.body = NULL;
}

static void
was_client_output_abort(void *ctx)
{
    struct was_client *client = ctx;

    assert(client->request.body != NULL);

    client->request.body = NULL;
    was_client_abort(client);
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
    struct was_client *client = ctx;

    assert(was_client_response_submitted(client));
    assert(client->response.body != NULL);

    client->response.body = NULL;

    if (client->request.body != NULL ||
        !was_control_is_empty(client->control)) {
        was_client_abort_response_body(client);
        return;
    }

    was_control_free(client->control);
    client->control = NULL;

    p_lease_release(&client->lease_ref, true, client->pool);
    pool_unref(client->caller_pool);
    pool_unref(client->pool);
}

static void
was_client_input_abort(void *ctx)
{
    struct was_client *client = ctx;

    assert(was_client_response_submitted(client));
    assert(client->response.body != NULL);

    client->response.body = NULL;

    was_client_abort_response_body(client);
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
    return (struct was_client*)(((char*)ao) - offsetof(struct was_client, async));
}

static void
was_client_request_abort(struct async_operation *ao)
{
    struct was_client *client = async_to_was_client(ao);

    /* async_abort() can only be used before the response was
       delivered to our callback */
    assert(!was_client_response_submitted(client));

    pool_unref(client->caller_pool);

    if (client->request.body != NULL)
        was_output_free_p(&client->request.body);

    if (client->response.body != NULL)
        was_input_free_p(&client->response.body);

    was_control_free(client->control);

    p_lease_release(&client->lease_ref, false, client->pool);
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
was_client_request(pool_t caller_pool, int control_fd,
                   int input_fd, int output_fd,
                   const struct lease *lease, void *lease_ctx,
                   http_method_t method, const char *uri,
                   const char *script_name, const char *path_info,
                   const char *query_string,
                   struct strmap *headers, istream_t body,
                   const char *const params[], unsigned num_params,
                   const struct http_response_handler *handler,
                   void *handler_ctx,
                   struct async_operation_ref *async_ref)
{
    assert(http_method_is_valid(method));
    assert(uri != NULL);

    pool_t pool = pool_new_linear(caller_pool, "was_client_request", 32768);
    struct was_client *client = p_malloc(pool, sizeof(*client));
    client->pool = pool;
    pool_ref(caller_pool);
    client->caller_pool = caller_pool;

    client->control = was_control_new(pool, control_fd,
                                      &was_client_control_handler, client);

    p_lease_ref_set(&client->lease_ref, lease, lease_ctx,
                    pool, "was_client_lease");

    http_response_handler_set(&client->handler, handler, handler_ctx);

    async_init(&client->async, &was_client_async_operation);
    async_ref_set(async_ref, &client->async);

    client->request.body = body != NULL
        ? was_output_new(pool, output_fd, body,
                         &was_client_output_handler, client)
        : NULL;

    client->response.status = HTTP_STATUS_OK;
    client->response.headers = strmap_new(pool, 41);
    client->response.body = !http_method_is_empty(method)
        ? was_input_new(pool, input_fd, &was_client_input_handler, client)
        : NULL;

    uint32_t method32 = (uint32_t)method;

    was_control_bulk_on(client->control);

    if (!was_control_send_empty(client->control, WAS_COMMAND_REQUEST) ||
        (method != HTTP_METHOD_GET &&
         !was_control_send(client->control, WAS_COMMAND_METHOD,
                           &method32, sizeof(method32))) ||
        !was_control_send_string(client->control, WAS_COMMAND_URI, uri))
        return;

    if ((script_name != NULL &&
         !was_control_send_string(client->control, WAS_COMMAND_SCRIPT_NAME,
                                  script_name)) ||
        (path_info != NULL &&
         !was_control_send_string(client->control, WAS_COMMAND_PATH_INFO,
                                  path_info)) ||
        (query_string != NULL &&
         !was_control_send_string(client->control, WAS_COMMAND_QUERY_STRING,
                                  query_string)) ||
        (headers != NULL &&
         !was_control_send_strmap(client->control, WAS_COMMAND_HEADER,
                                  headers)) ||
        !was_control_send_array(client->control, WAS_COMMAND_PARAMETER,
                                params, num_params) ||
        !was_control_send_empty(client->control, client->request.body != NULL
                                ? WAS_COMMAND_DATA : WAS_COMMAND_NO_DATA)) {
        was_client_abort_response_headers(client);
        return;
    }

    was_control_bulk_off(client->control);
}
