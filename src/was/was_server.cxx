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
#include "http_response.hxx"
#include "async.hxx"
#include "direct.hxx"
#include "istream/istream.hxx"
#include "strmap.hxx"
#include "pool.hxx"
#include "util/ConstBuffer.hxx"

#include <was/protocol.h>
#include <daemon/log.h>

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

struct WasServer final : WasControlHandler {
    struct pool *pool;

    int control_fd, input_fd, output_fd;

    WasControl *control;

    const WasServerHandler *handler;
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

        WasInput *body;
    } request;

    struct {
        http_status_t status;


        WasOutput *body;
    } response;

    /* virtual methods from class WasControlHandler */
    bool OnWasControlPacket(enum was_command cmd,
                            ConstBuffer<void> payload) override;

    void OnWasControlDone() override {
    }

    void OnWasControlError(GError *error) override;
};

static void
was_server_release(WasServer *server, GError *error)
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
was_server_release_unused(WasServer *server)
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
was_server_abort(WasServer *server, GError *error)
{
    was_server_release(server, error);

    server->handler->free(server->handler_ctx);
}

/**
 * Abort receiving the response status/headers from the WAS server.
 */
static void
was_server_abort_unused(WasServer *server)
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
    WasServer *server = (WasServer *)ctx;

    assert(server->control != nullptr);
    assert(server->response.body != nullptr);

    return was_control_send_uint64(server->control,
                                   WAS_COMMAND_LENGTH, length);
}

static bool
was_server_output_premature(uint64_t length, GError *error, void *ctx)
{
    WasServer *server = (WasServer *)ctx;

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
    WasServer *server = (WasServer *)ctx;

    assert(server->response.body != nullptr);

    server->response.body = nullptr;
}

static void
was_server_output_abort(GError *error, void *ctx)
{
    WasServer *server = (WasServer *)ctx;

    assert(server->response.body != nullptr);

    server->response.body = nullptr;
    was_server_abort(server, error);
}

static constexpr WasOutputHandler was_server_output_handler = {
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
    WasServer *server = (WasServer *)ctx;

    assert(server->request.headers == nullptr);
    assert(server->request.body != nullptr);

    server->request.body = nullptr;

    // XXX
}

static void
was_server_input_abort(void *ctx)
{
    WasServer *server = (WasServer *)ctx;

    assert(server->request.headers == nullptr);
    assert(server->request.body != nullptr);

    server->request.body = nullptr;

    was_server_abort_unused(server);
}

static constexpr WasInputHandler was_server_input_handler = {
    .eof = was_server_input_eof,
    .premature = nullptr,
    .abort = was_server_input_abort,
};


/*
 * Control channel handler
 */

bool
WasServer::OnWasControlPacket(enum was_command cmd, ConstBuffer<void> payload)
{
    GError *error;

    switch (cmd) {
        struct strmap *headers;
        const uint64_t *length_p;
        const char *p;
        http_method_t method;

    case WAS_COMMAND_NOP:
        break;

    case WAS_COMMAND_REQUEST:
        if (request.pool != nullptr) {
            error = g_error_new_literal(was_quark(), 0,
                                        "misplaced REQUEST packet");
            was_server_abort(this, error);
            return false;
        }

        request.pool = pool_new_linear(pool, "was_server_request", 32768);
        request.method = HTTP_METHOD_GET;
        request.uri = nullptr;
        request.headers = strmap_new(request.pool);
        request.body = nullptr;
        response.body = nullptr;
        break;

    case WAS_COMMAND_METHOD:
        if (payload.size != sizeof(method)) {
            error = g_error_new_literal(was_quark(), 0,
                                        "malformed METHOD packet");
            was_server_abort(this, error);
            return false;
        }

        method = *(const http_method_t *)payload.data;
        if (request.method != HTTP_METHOD_GET &&
            method != request.method) {
            /* sending that packet twice is illegal */
            error = g_error_new_literal(was_quark(), 0,
                                        "misplaced METHOD packet");
            was_server_abort(this, error);
            return false;
        }

        if (!http_method_is_valid(method)) {
            error = g_error_new_literal(was_quark(), 0,
                                        "invalid METHOD packet");
            was_server_abort(this, error);
            return false;
        }

        request.method = method;
        break;

    case WAS_COMMAND_URI:
        if (request.pool == nullptr || request.uri != nullptr) {
            error = g_error_new_literal(was_quark(), 0,
                                        "misplaced URI packet");
            was_server_abort(this, error);
            return false;
        }

        request.uri = p_strndup(request.pool,
                                (const char *)payload.data, payload.size);
        break;

    case WAS_COMMAND_SCRIPT_NAME:
    case WAS_COMMAND_PATH_INFO:
    case WAS_COMMAND_QUERY_STRING:
        // XXX
        break;

    case WAS_COMMAND_HEADER:
        if (request.pool == nullptr || request.headers == nullptr) {
            error = g_error_new_literal(was_quark(), 0,
                                        "misplaced HEADER packet");
            was_server_abort(this, error);
            return false;
        }

        p = (const char *)memchr(payload.data, '=', payload.size);
        if (p == nullptr) {
            error = g_error_new_literal(was_quark(), 0,
                                        "malformed HEADER packet");
            was_server_abort(this, error);
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
        was_server_abort(this, error);
        return false;

    case WAS_COMMAND_NO_DATA:
        if (request.pool == nullptr || request.uri == nullptr ||
            request.headers == nullptr) {
            error = g_error_new_literal(was_quark(), 0,
                                        "misplaced NO_DATA packet");
            was_server_abort(this, error);
            return false;
        }

        headers = request.headers;
        request.headers = nullptr;

        request.body = nullptr;

        handler->request(request.pool, request.method,
                         request.uri, headers, nullptr,
                         handler_ctx);
        /* XXX check if connection has been closed */
        break;

    case WAS_COMMAND_DATA:
        if (request.pool == nullptr || request.uri == nullptr ||
            request.headers == nullptr) {
            error = g_error_new_literal(was_quark(), 0,
                                        "misplaced DATA packet");
            was_server_abort(this, error);
            return false;
        }

        headers = request.headers;
        request.headers = nullptr;

        request.body = was_input_new(request.pool,
                                             input_fd,
                                             &was_server_input_handler,
                                             this);

        handler->request(request.pool, request.method,
                         request.uri, headers,
                         &was_input_enable(*request.body),
                         handler_ctx);
        /* XXX check if connection has been closed */
        break;

    case WAS_COMMAND_LENGTH:
        if (request.pool == nullptr || request.headers != nullptr) {
            error = g_error_new_literal(was_quark(), 0,
                                        "misplaced LENGTH packet");
            was_server_abort(this, error);
            return false;
        }

        length_p = (const uint64_t *)payload.data;
        if (response.body == nullptr ||
            payload.size != sizeof(*length_p)) {
            error = g_error_new_literal(was_quark(), 0,
                                        "malformed LENGTH packet");
            was_server_abort(this, error);
            return false;
        }

        if (!was_input_set_length(request.body, *length_p)) {
            error = g_error_new_literal(was_quark(), 0,
                                        "invalid LENGTH packet");
            was_server_abort(this, error);
            return false;
        }

        break;

    case WAS_COMMAND_STOP:
    case WAS_COMMAND_PREMATURE:
        // XXX
        error = g_error_new(was_quark(), 0,
                            "unexpected packet: %d", cmd);
        was_server_abort(this, error);
        return false;
    }

    return true;
}

void
WasServer::OnWasControlError(GError *error)
{
    was_server_abort(this, error);
}

/*
 * constructor
 *
 */

WasServer *
was_server_new(struct pool *pool, int control_fd, int input_fd, int output_fd,
               const WasServerHandler *handler, void *handler_ctx)
{
    assert(pool != nullptr);
    assert(control_fd >= 0);
    assert(input_fd >= 0);
    assert(output_fd >= 0);
    assert(handler != nullptr);
    assert(handler->request != nullptr);
    assert(handler->free != nullptr);

    auto server = NewFromPool<WasServer>(*pool);
    server->pool = pool;
    server->control_fd = control_fd;
    server->input_fd = input_fd;
    server->output_fd = output_fd;

    server->control = was_control_new(pool, control_fd, *server);

    server->handler = handler;
    server->handler_ctx = handler_ctx;

    server->request.pool = nullptr;

    return server;
}

void
was_server_free(WasServer *server)
{
    GError *error = g_error_new_literal(was_quark(), 0,
                                        "shutting down WAS connection");
    was_server_release(server, error);
}

void
was_server_response(WasServer *server, http_status_t status,
                    struct strmap *headers, Istream *body)
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
        server->response.body = was_output_new(*server->request.pool,
                                               server->output_fd, *body,
                                               was_server_output_handler,
                                               server);
        if (!was_control_send_empty(server->control, WAS_COMMAND_DATA))
            return;

        off_t available = body->GetAvailable(false);
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
