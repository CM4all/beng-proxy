/*
 * Web Application Socket client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "was_server.hxx"
#include "was_quark.h"
#include "was_control.hxx"
#include "was_output.hxx"
#include "was_input.hxx"
#include "http_response.h"
#include "async.h"
#include "pevent.h"
#include "direct.h"
#include "istream-internal.h"
#include "fifo-buffer.h"
#include "fd-util.h"
#include "strmap.h"

#include <was/protocol.h>
#include <daemon/log.h>

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

struct was_server {
    struct pool *pool;

    int control_fd, input_fd, output_fd;

    struct was_control *control;

    const struct was_server_handler *handler;
    void *handler_ctx;

    struct {
        struct pool *pool;

        http_method_t method;

        const char *uri;

        /**
         * Request headers being assembled.  This pointer is set to
         * nullptr before before the request is dispatched to the
         * handler.
         */
        struct strmap *headers;

        struct was_input *body;
    } request;

    struct {
        http_status_t status;


        struct was_output *body;
    } response;
};

static void
was_server_release(struct was_server *server, GError *error)
{
    if (server->request.pool != nullptr) {
        if (server->request.body != nullptr)
            was_input_free_p(&server->request.body, error);
        else
            g_error_free(error);

        if (server->request.headers == nullptr && server->response.body != nullptr)
            was_output_free_p(&server->response.body);

        pool_unref(server->request.pool);
    } else
        g_error_free(error);

    close(server->control_fd);
    close(server->input_fd);
    close(server->output_fd);
}

static void
was_server_release_unused(struct was_server *server)
{
    if (server->request.pool != nullptr) {
        if (server->request.body != nullptr)
            was_input_free_unused_p(&server->request.body);

        if (server->request.headers == nullptr && server->response.body != nullptr)
            was_output_free_p(&server->response.body);

        pool_unref(server->request.pool);
    }

    close(server->control_fd);
    close(server->input_fd);
    close(server->output_fd);
}

/**
 * Abort receiving the response status/headers from the WAS server.
 */
static void
was_server_abort(struct was_server *server, GError *error)
{
    was_server_release(server, error);

    server->handler->free(server->handler_ctx);
}

/**
 * Abort receiving the response status/headers from the WAS server.
 */
static void
was_server_abort_unused(struct was_server *server)
{
    was_server_release_unused(server);

    server->handler->free(server->handler_ctx);
}

/*
 * Output handler
 */

static bool
was_server_output_length(uint64_t length, void *ctx)
{
    struct was_server *server = (struct was_server *)ctx;

    assert(server->control != nullptr);
    assert(server->response.body != nullptr);

    return was_control_send_uint64(server->control,
                                   WAS_COMMAND_LENGTH, length);
}

static bool
was_server_output_premature(uint64_t length, GError *error, void *ctx)
{
    struct was_server *server = (struct was_server *)ctx;

    assert(server->control != nullptr);
    assert(server->response.body != nullptr);

    server->response.body = nullptr;

    /* XXX send PREMATURE, recover */
    (void)length;
    was_server_abort(server, error);
    return false;
}

static void
was_server_output_eof(void *ctx)
{
    struct was_server *server = (struct was_server *)ctx;

    assert(server->response.body != nullptr);

    server->response.body = nullptr;
}

static void
was_server_output_abort(GError *error, void *ctx)
{
    struct was_server *server = (struct was_server *)ctx;

    assert(server->response.body != nullptr);

    server->response.body = nullptr;
    was_server_abort(server, error);
}

static const struct was_output_handler was_server_output_handler = {
    .length = was_server_output_length,
    .premature = was_server_output_premature,
    .eof = was_server_output_eof,
    .abort = was_server_output_abort,
};


/*
 * Input handler
 */

static void
was_server_input_eof(void *ctx)
{
    struct was_server *server = (struct was_server *)ctx;

    assert(server->request.headers == nullptr);
    assert(server->request.body != nullptr);

    server->request.body = nullptr;

    // XXX
}

static void
was_server_input_abort(void *ctx)
{
    struct was_server *server = (struct was_server *)ctx;

    assert(server->request.headers == nullptr);
    assert(server->request.body != nullptr);

    server->request.body = nullptr;

    was_server_abort_unused(server);
}

static const struct was_input_handler was_server_input_handler = {
    .eof = was_server_input_eof,
    .abort = was_server_input_abort,
};


/*
 * Control channel handler
 */

static bool
was_server_control_packet(enum was_command cmd, const void *payload,
                          size_t payload_length, void *ctx)
{
    struct was_server *server = (struct was_server *)ctx;
    GError *error;

    switch (cmd) {
        struct strmap *headers;
        const uint64_t *length_p;
        const char *p;
        http_method_t method;

    case WAS_COMMAND_NOP:
        break;

    case WAS_COMMAND_REQUEST:
        if (server->request.pool != nullptr) {
            error = g_error_new_literal(was_quark(), 0,
                                        "misplaced REQUEST packet");
            was_server_abort(server, error);
            return false;
        }

        server->request.pool = pool_new_linear(server->pool,
                                               "was_server_request", 32768);
        server->request.method = HTTP_METHOD_GET;
        server->request.uri = nullptr;
        server->request.headers = strmap_new(server->request.pool, 41);
        server->request.body = nullptr;
        server->response.body = nullptr;
        break;

    case WAS_COMMAND_METHOD:
        if (payload_length != sizeof(method)) {
            error = g_error_new_literal(was_quark(), 0,
                                        "malformed METHOD packet");
            was_server_abort(server, error);
            return false;
        }

        method = *(const http_method_t *)payload;
        if (server->request.method != HTTP_METHOD_GET &&
            method != server->request.method) {
            /* sending that packet twice is illegal */
            error = g_error_new_literal(was_quark(), 0,
                                        "misplaced METHOD packet");
            was_server_abort(server, error);
            return false;
        }

        if (!http_method_is_valid(method)) {
            error = g_error_new_literal(was_quark(), 0,
                                        "invalid METHOD packet");
            was_server_abort(server, error);
            return false;
        }

        server->request.method = method;
        break;

    case WAS_COMMAND_URI:
        if (server->request.pool == nullptr || server->request.uri != nullptr) {
            error = g_error_new_literal(was_quark(), 0,
                                        "misplaced URI packet");
            was_server_abort(server, error);
            return false;
        }

        server->request.uri = p_strndup(server->request.pool,
                                        (const char *)payload, payload_length);
        break;

    case WAS_COMMAND_SCRIPT_NAME:
    case WAS_COMMAND_PATH_INFO:
    case WAS_COMMAND_QUERY_STRING:
        // XXX
        break;

    case WAS_COMMAND_HEADER:
        if (server->request.pool == nullptr || server->request.headers == nullptr) {
            error = g_error_new_literal(was_quark(), 0,
                                        "misplaced HEADER packet");
            was_server_abort(server, error);
            return false;
        }

        p = (const char *)memchr(payload, '=', payload_length);
        if (p == nullptr) {
            error = g_error_new_literal(was_quark(), 0,
                                        "malformed HEADER packet");
            was_server_abort(server, error);
            return false;
        }

        // XXX parse buffer

        break;

    case WAS_COMMAND_PARAMETER:
        // XXX
        break;

    case WAS_COMMAND_STATUS:
        error = g_error_new_literal(was_quark(), 0,
                                    "misplaced STATUS packet");
        was_server_abort(server, error);
        return false;

    case WAS_COMMAND_NO_DATA:
        if (server->request.pool == nullptr || server->request.uri == nullptr ||
            server->request.headers == nullptr) {
            error = g_error_new_literal(was_quark(), 0,
                                        "misplaced NO_DATA packet");
            was_server_abort(server, error);
            return false;
        }

        headers = server->request.headers;
        server->request.headers = nullptr;

        server->request.body = nullptr;

        server->handler->request(server->request.pool, server->request.method,
                                 server->request.uri, headers, nullptr,
                                 server->handler_ctx);
        /* XXX check if connection has been closed */
        break;

    case WAS_COMMAND_DATA:
        if (server->request.pool == nullptr || server->request.uri == nullptr ||
            server->request.headers == nullptr) {
            error = g_error_new_literal(was_quark(), 0,
                                        "misplaced DATA packet");
            was_server_abort(server, error);
            return false;
        }

        headers = server->request.headers;
        server->request.headers = nullptr;

        server->request.body = was_input_new(server->request.pool,
                                             server->input_fd,
                                             &was_server_input_handler,
                                             server);

        server->handler->request(server->request.pool, server->request.method,
                                 server->request.uri, headers,
                                 was_input_enable(server->request.body),
                                 server->handler_ctx);
        /* XXX check if connection has been closed */
        break;

    case WAS_COMMAND_LENGTH:
        if (server->request.pool == nullptr || server->request.headers != nullptr) {
            error = g_error_new_literal(was_quark(), 0,
                                        "misplaced LENGTH packet");
            was_server_abort(server, error);
            return false;
        }

        length_p = (const uint64_t *)payload;
        if (server->response.body == nullptr ||
            payload_length != sizeof(*length_p)) {
            error = g_error_new_literal(was_quark(), 0,
                                        "malformed LENGTH packet");
            was_server_abort(server, error);
            return false;
        }

        if (!was_input_set_length(server->request.body, *length_p)) {
            error = g_error_new_literal(was_quark(), 0,
                                        "invalid LENGTH packet");
            was_server_abort(server, error);
            return false;
        }

        break;

    case WAS_COMMAND_STOP:
    case WAS_COMMAND_PREMATURE:
        // XXX
        error = g_error_new(was_quark(), 0,
                            "unexpected packet: %d", cmd);
        was_server_abort(server, error);
        return false;
    }

    (void)payload;
    (void)payload_length;

    return true;
}

static void
was_server_control_eof(void *ctx)
{
    struct was_server *server = (struct was_server *)ctx;

    (void)server;
}

static void
was_server_control_abort(GError *error, void *ctx)
{
    struct was_server *server = (struct was_server *)ctx;

    was_server_abort(server, error);
}

static const struct was_control_handler was_server_control_handler = {
    .packet = was_server_control_packet,
    .eof = was_server_control_eof,
    .abort = was_server_control_abort,
};


/*
 * constructor
 *
 */

struct was_server *
was_server_new(struct pool *pool, int control_fd, int input_fd, int output_fd,
               const struct was_server_handler *handler, void *handler_ctx)
{
    assert(pool != nullptr);
    assert(control_fd >= 0);
    assert(input_fd >= 0);
    assert(output_fd >= 0);
    assert(handler != nullptr);
    assert(handler->request != nullptr);
    assert(handler->free != nullptr);

    auto server = NewFromPool<struct was_server>(pool);
    server->pool = pool;
    server->control_fd = control_fd;
    server->input_fd = input_fd;
    server->output_fd = output_fd;

    server->control = was_control_new(pool, control_fd,
                                      &was_server_control_handler, server);

    server->handler = handler;
    server->handler_ctx = handler_ctx;

    server->request.pool = nullptr;

    return server;
}

void
was_server_free(struct was_server *server)
{
    GError *error = g_error_new_literal(was_quark(), 0,
                                        "shutting down WAS connection");
    was_server_release(server, error);
}

void
was_server_response(struct was_server *server, http_status_t status,
                    struct strmap *headers, struct istream *body)
{
    assert(server != nullptr);
    assert(server->request.pool != nullptr);
    assert(server->request.headers == nullptr);
    assert(server->response.body == nullptr);
    assert(http_status_is_valid(status));
    assert(!http_status_is_empty(status) || body == nullptr);

    if (!was_control_send(server->control, WAS_COMMAND_STATUS,
                          &status, sizeof(status)))
        return;

    if (body != nullptr) {
        server->response.body = was_output_new(server->request.pool,
                                               server->output_fd, body,
                                               &was_server_output_handler,
                                               server);
        if (!was_control_send_empty(server->control, WAS_COMMAND_DATA))
            return;

        off_t available = istream_available(body, false);
        if (available >= 0 &&
            !was_control_send_uint64(server->control, WAS_COMMAND_LENGTH,
                                     available))
            return;
    } else {
        if (!was_control_send_empty(server->control, WAS_COMMAND_NO_DATA))
            return;
    }

    (void)headers; // XXX
}
