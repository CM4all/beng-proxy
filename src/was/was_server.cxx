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
#include "istream/istream_null.hxx"
#include "strmap.hxx"
#include "pool.hxx"
#include "util/ConstBuffer.hxx"

#include <was/protocol.h>
#include <daemon/log.h>

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

struct WasServer final : WasControlHandler, WasOutputHandler, WasInputHandler {
    struct pool *const pool;

    const int control_fd, input_fd, output_fd;

    WasControl *control;

    WasServerHandler &handler;

    struct {
        struct pool *pool = nullptr;

        http_method_t method;

        const char *uri;

        /**
         * Request headers being assembled.  This pointer is set to
         * nullptr before before the request is dispatched to the
         * handler.
         */
        struct strmap *headers;

        WasInput *body;

        bool pending = false;
    } request;

    struct {
        http_status_t status;


        WasOutput *body;
    } response;

    WasServer(struct pool &_pool,
              int _control_fd, int _input_fd, int _output_fd,
              WasServerHandler &_handler)
        :pool(&_pool),
         control_fd(_control_fd), input_fd(_input_fd), output_fd(_output_fd),
         control(was_control_new(pool, control_fd, *this)),
         handler(_handler) {}

    void CloseFiles() {
        close(control_fd);
        close(input_fd);
        close(output_fd);
    }

    void ReleaseError(GError *error);
    void ReleaseUnused();

    /**
     * Abort receiving the response status/headers from the WAS server.
     */
    void AbortError(GError *error) {
        ReleaseError(error);
        handler.OnWasClosed();
    }

    /**
     * Abort receiving the response status/headers from the WAS server.
     */
    void AbortUnused() {
        ReleaseUnused();
        handler.OnWasClosed();
    }

    /* virtual methods from class WasControlHandler */
    bool OnWasControlPacket(enum was_command cmd,
                            ConstBuffer<void> payload) override;

    bool OnWasControlDrained() override {
        if (request.pending) {
            auto *headers = request.headers;
            request.headers = nullptr;

            Istream *body = nullptr;
            if (request.body != nullptr)
                body = &was_input_enable(*request.body);

            handler.OnWasRequest(*request.pool, request.method,
                                 request.uri, std::move(*headers),
                                 body);
            /* XXX check if connection has been closed */
        }

        return true;
    }

    void OnWasControlDone() override {
        control = nullptr;
    }

    void OnWasControlError(GError *error) override;

    /* virtual methods from class WasOutputHandler */
    bool WasOutputLength(uint64_t length) override;
    bool WasOutputPremature(uint64_t length, GError *error) override;
    void WasOutputEof() override;
    void WasOutputError(GError *error) override;

    /* virtual methods from class WasInputHandler */
    void WasInputClose(uint64_t received) override;
    void WasInputRelease() override;
    void WasInputEof() override;
    void WasInputError() override;
};

void
WasServer::ReleaseError(GError *error)
{
    if (control != nullptr) {
        was_control_free(control);
        control = nullptr;
    }

    if (request.pool != nullptr) {
        if (request.body != nullptr)
            was_input_free_p(&request.body, error);
        else
            g_error_free(error);

        if (request.headers == nullptr && response.body != nullptr)
            was_output_free_p(&response.body);

        pool_unref(request.pool);
    } else
        g_error_free(error);

    CloseFiles();
}

void
WasServer::ReleaseUnused()
{
    if (control != nullptr) {
        was_control_free(control);
        control = nullptr;
    }

    if (request.pool != nullptr) {
        if (request.body != nullptr)
            was_input_free_unused_p(&request.body);

        if (request.headers == nullptr && response.body != nullptr)
            was_output_free_p(&response.body);

        pool_unref(request.pool);
    }

    CloseFiles();
}

/*
 * Output handler
 */

bool
WasServer::WasOutputLength(uint64_t length)
{
    assert(control != nullptr);
    assert(response.body != nullptr);

    return was_control_send_uint64(control, WAS_COMMAND_LENGTH, length);
}

bool
WasServer::WasOutputPremature(uint64_t length, GError *error)
{
    if (control == nullptr)
        /* this can happen if was_input_free() call destroys the
           WasOutput instance; this check means to work around this
           circular call */
        return true;

    assert(response.body != nullptr);

    response.body = nullptr;

    /* XXX send PREMATURE, recover */
    (void)length;
    AbortError(error);
    return false;
}

void
WasServer::WasOutputEof()
{
    assert(response.body != nullptr);

    response.body = nullptr;
}

void
WasServer::WasOutputError(GError *error)
{
    assert(response.body != nullptr);

    response.body = nullptr;
    AbortError(error);
}

/*
 * Input handler
 */

void
WasServer::WasInputClose(gcc_unused uint64_t received)
{
    /* this happens when the request handler isn't interested in the
       request body */

    assert(request.headers == nullptr);
    assert(request.body != nullptr);

    request.body = nullptr;

    if (control != nullptr)
        was_control_send_empty(control, WAS_COMMAND_STOP);

    // TODO: handle PREMATURE packet which we'll receive soon
}

void
WasServer::WasInputRelease()
{
}

void
WasServer::WasInputEof()
{
    assert(request.headers == nullptr);
    assert(request.body != nullptr);

    request.body = nullptr;

    // TODO
}

void
WasServer::WasInputError()
{
    assert(request.headers == nullptr);
    assert(request.body != nullptr);

    request.body = nullptr;

    AbortUnused();
}

/*
 * Control channel handler
 */

bool
WasServer::OnWasControlPacket(enum was_command cmd, ConstBuffer<void> payload)
{
    GError *error;

    switch (cmd) {
        const uint64_t *length_p;
        const char *p;
        http_method_t method;

    case WAS_COMMAND_NOP:
        break;

    case WAS_COMMAND_REQUEST:
        if (request.pool != nullptr) {
            error = g_error_new_literal(was_quark(), 0,
                                        "misplaced REQUEST packet");
            AbortError(error);
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
            AbortError(error);
            return false;
        }

        method = *(const http_method_t *)payload.data;
        if (request.method != HTTP_METHOD_GET &&
            method != request.method) {
            /* sending that packet twice is illegal */
            error = g_error_new_literal(was_quark(), 0,
                                        "misplaced METHOD packet");
            AbortError(error);
            return false;
        }

        if (!http_method_is_valid(method)) {
            error = g_error_new_literal(was_quark(), 0,
                                        "invalid METHOD packet");
            AbortError(error);
            return false;
        }

        request.method = method;
        break;

    case WAS_COMMAND_URI:
        if (request.pool == nullptr || request.uri != nullptr) {
            error = g_error_new_literal(was_quark(), 0,
                                        "misplaced URI packet");
            AbortError(error);
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
            AbortError(error);
            return false;
        }

        p = (const char *)memchr(payload.data, '=', payload.size);
        if (p == nullptr) {
            error = g_error_new_literal(was_quark(), 0,
                                        "malformed HEADER packet");
            AbortError(error);
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
        AbortError(error);
        return false;

    case WAS_COMMAND_NO_DATA:
        if (request.pool == nullptr || request.uri == nullptr ||
            request.headers == nullptr) {
            error = g_error_new_literal(was_quark(), 0,
                                        "misplaced NO_DATA packet");
            AbortError(error);
            return false;
        }

        request.body = nullptr;
        request.pending = true;
        break;

    case WAS_COMMAND_DATA:
        if (request.pool == nullptr || request.uri == nullptr ||
            request.headers == nullptr) {
            error = g_error_new_literal(was_quark(), 0,
                                        "misplaced DATA packet");
            AbortError(error);
            return false;
        }

        request.body = was_input_new(request.pool,
                                             input_fd,
                                             *this);
        request.pending = true;
        break;

    case WAS_COMMAND_LENGTH:
        if (request.pool == nullptr ||
            request.body == nullptr ||
            (request.headers != nullptr && !request.pending)) {
            error = g_error_new_literal(was_quark(), 0,
                                        "misplaced LENGTH packet");
            AbortError(error);
            return false;
        }

        length_p = (const uint64_t *)payload.data;
        if (payload.size != sizeof(*length_p)) {
            error = g_error_new_literal(was_quark(), 0,
                                        "malformed LENGTH packet");
            AbortError(error);
            return false;
        }

        if (!was_input_set_length(request.body, *length_p)) {
            error = g_error_new_literal(was_quark(), 0,
                                        "invalid LENGTH packet");
            AbortError(error);
            return false;
        }

        break;

    case WAS_COMMAND_STOP:
    case WAS_COMMAND_PREMATURE:
        // XXX
        error = g_error_new(was_quark(), 0,
                            "unexpected packet: %d", cmd);
        AbortError(error);
        return false;
    }

    return true;
}

void
WasServer::OnWasControlError(GError *error)
{
    control = nullptr;

    AbortError(error);
}

/*
 * constructor
 *
 */

WasServer *
was_server_new(struct pool *pool, int control_fd, int input_fd, int output_fd,
               WasServerHandler &handler)
{
    assert(pool != nullptr);
    assert(control_fd >= 0);
    assert(input_fd >= 0);
    assert(output_fd >= 0);

    return NewFromPool<WasServer>(*pool, *pool,
                                  control_fd, input_fd, output_fd,
                                  handler);
}

void
was_server_free(WasServer *server)
{
    GError *error = g_error_new_literal(was_quark(), 0,
                                        "shutting down WAS connection");
    server->ReleaseError(error);
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

    was_control_bulk_on(server->control);

    if (!was_control_send(server->control, WAS_COMMAND_STATUS,
                          &status, sizeof(status)))
        return;

    if (body != nullptr && http_method_is_empty(server->request.method)) {
        if (server->request.method == HTTP_METHOD_HEAD) {
            off_t available = body->GetAvailable(false);
            if (available >= 0) {
                if (headers == nullptr)
                    headers = strmap_new(server->request.pool);
                headers->Set("content-length",
                             p_sprintf(server->request.pool, "%lu",
                                       (unsigned long)available));
            }
        }

        body->CloseUnused();
        body = nullptr;
    }

    if (headers != nullptr)
        was_control_send_strmap(server->control, WAS_COMMAND_HEADER,
                                headers);

    if (body != nullptr) {
        server->response.body = was_output_new(*server->request.pool,
                                               server->output_fd, *body,
                                               *server);
        if (!was_control_send_empty(server->control, WAS_COMMAND_DATA) ||
            !was_output_check_length(*server->response.body))
            return;
    } else {
        if (!was_control_send_empty(server->control, WAS_COMMAND_NO_DATA))
            return;
    }

    was_control_bulk_off(server->control);
}
