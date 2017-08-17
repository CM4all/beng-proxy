/*
 * Copyright 2007-2017 Content Management AG
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

#include "Server.hxx"
#include "Error.hxx"
#include "Control.hxx"
#include "Output.hxx"
#include "Input.hxx"
#include "http_response.hxx"
#include "direct.hxx"
#include "istream/istream.hxx"
#include "istream/istream_null.hxx"
#include "strmap.hxx"
#include "pool.hxx"
#include "io/FileDescriptor.hxx"
#include "util/ConstBuffer.hxx"
#include "util/StringFormat.hxx"

#include <was/protocol.h>

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

struct WasServer final : WasControlHandler, WasOutputHandler, WasInputHandler {
    struct pool &pool;

    const int control_fd, input_fd;
    FileDescriptor output_fd;

    WasControl control;

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
        StringMap *headers;

        WasInput *body;

        bool released = false;

        bool pending = false;
    } request;

    struct {
        http_status_t status;

        WasOutput *body;
    } response;

    WasServer(struct pool &_pool, EventLoop &event_loop,
              int _control_fd, int _input_fd, FileDescriptor _output_fd,
              WasServerHandler &_handler)
        :pool(_pool),
         control_fd(_control_fd), input_fd(_input_fd), output_fd(_output_fd),
         control(event_loop, control_fd, *this),
         handler(_handler) {}

    void CloseFiles() {
        close(control_fd);
        close(input_fd);
        output_fd.Close();
    }

    void ReleaseError(std::exception_ptr ep);

    void ReleaseError(const char *msg) {
        ReleaseError(std::make_exception_ptr(WasProtocolError(msg)));
    }

    void ReleaseUnused();

    /**
     * Abort receiving the response status/headers from the WAS server.
     */
    void AbortError(std::exception_ptr ep) {
        ReleaseError(ep);
        handler.OnWasClosed();
    }

    void AbortError(const char *msg) {
        AbortError(std::make_exception_ptr(WasProtocolError(msg)));
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
            if (request.released) {
                was_input_free_unused(request.body);
                request.body = nullptr;

                body = istream_null_new(request.pool);
            } else if (request.body != nullptr)
                body = &was_input_enable(*request.body);

            handler.OnWasRequest(*request.pool, request.method,
                                 request.uri, std::move(*headers),
                                 body);
            /* XXX check if connection has been closed */
        }

        return true;
    }

    void OnWasControlDone() override {
        assert(!control.IsDefined());
    }

    void OnWasControlError(std::exception_ptr ep) override;

    /* virtual methods from class WasOutputHandler */
    bool WasOutputLength(uint64_t length) override;
    bool WasOutputPremature(uint64_t length, std::exception_ptr ep) override;
    void WasOutputEof() override;
    void WasOutputError(std::exception_ptr ep) override;

    /* virtual methods from class WasInputHandler */
    void WasInputClose(uint64_t received) override;
    bool WasInputRelease() override;
    void WasInputEof() override;
    void WasInputError() override;
};

void
WasServer::ReleaseError(std::exception_ptr ep)
{
    if (control.IsDefined())
        control.ReleaseSocket();

    if (request.pool != nullptr) {
        if (request.body != nullptr)
            was_input_free_p(&request.body, ep);

        if (request.headers == nullptr && response.body != nullptr)
            was_output_free_p(&response.body);

        pool_unref(request.pool);
    }

    CloseFiles();
}

void
WasServer::ReleaseUnused()
{
    if (control.IsDefined())
        control.ReleaseSocket();

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
    assert(control.IsDefined());
    assert(response.body != nullptr);

    return control.SendUint64(WAS_COMMAND_LENGTH, length);
}

bool
WasServer::WasOutputPremature(uint64_t length, std::exception_ptr ep)
{
    if (!control.IsDefined())
        /* this can happen if was_input_free() call destroys the
           WasOutput instance; this check means to work around this
           circular call */
        return true;

    assert(response.body != nullptr);

    response.body = nullptr;

    /* XXX send PREMATURE, recover */
    (void)length;
    AbortError(ep);
    return false;
}

void
WasServer::WasOutputEof()
{
    assert(response.body != nullptr);

    response.body = nullptr;
}

void
WasServer::WasOutputError(std::exception_ptr ep)
{
    assert(response.body != nullptr);

    response.body = nullptr;
    AbortError(ep);
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

    if (control.IsDefined())
        control.SendEmpty(WAS_COMMAND_STOP);

    // TODO: handle PREMATURE packet which we'll receive soon
}

bool
WasServer::WasInputRelease()
{
    assert(request.body != nullptr);
    assert(!request.released);

    request.released = true;
    return true;
}

void
WasServer::WasInputEof()
{
    assert(request.headers == nullptr);
    assert(request.body != nullptr);
    assert(request.released);

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
    switch (cmd) {
        const uint64_t *length_p;
        const char *p;
        http_method_t method;

    case WAS_COMMAND_NOP:
        break;

    case WAS_COMMAND_REQUEST:
        if (request.pool != nullptr) {
            AbortError("misplaced REQUEST packet");
            return false;
        }

        request.pool = pool_new_linear(&pool, "was_server_request", 32768);
        request.method = HTTP_METHOD_GET;
        request.uri = nullptr;
        request.headers = strmap_new(request.pool);
        request.body = nullptr;
        response.body = nullptr;
        break;

    case WAS_COMMAND_METHOD:
        if (payload.size != sizeof(method)) {
            AbortError("malformed METHOD packet");
            return false;
        }

        method = *(const http_method_t *)payload.data;
        if (request.method != HTTP_METHOD_GET &&
            method != request.method) {
            /* sending that packet twice is illegal */
            AbortError("misplaced METHOD packet");
            return false;
        }

        if (!http_method_is_valid(method)) {
            AbortError("invalid METHOD packet");
            return false;
        }

        request.method = method;
        break;

    case WAS_COMMAND_URI:
        if (request.pool == nullptr || request.uri != nullptr) {
            AbortError("misplaced URI packet");
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
            AbortError("misplaced HEADER packet");
            return false;
        }

        p = (const char *)memchr(payload.data, '=', payload.size);
        if (p == nullptr) {
            AbortError("malformed HEADER packet");
            return false;
        }

        // XXX parse buffer

        break;

    case WAS_COMMAND_PARAMETER:
        // XXX
        break;

    case WAS_COMMAND_STATUS:
        AbortError("misplaced STATUS packet");
        return false;

    case WAS_COMMAND_NO_DATA:
        if (request.pool == nullptr || request.uri == nullptr ||
            request.headers == nullptr) {
            AbortError("misplaced NO_DATA packet");
            return false;
        }

        request.body = nullptr;
        request.pending = true;
        break;

    case WAS_COMMAND_DATA:
        if (request.pool == nullptr || request.uri == nullptr ||
            request.headers == nullptr) {
            AbortError("misplaced DATA packet");
            return false;
        }

        request.body = was_input_new(*request.pool, control.GetEventLoop(),
                                     input_fd, *this);
        request.pending = true;
        break;

    case WAS_COMMAND_LENGTH:
        if (request.pool == nullptr ||
            request.body == nullptr ||
            (request.headers != nullptr && !request.pending)) {
            AbortError("misplaced LENGTH packet");
            return false;
        }

        length_p = (const uint64_t *)payload.data;
        if (payload.size != sizeof(*length_p)) {
            AbortError("malformed LENGTH packet");
            return false;
        }

        if (!was_input_set_length(request.body, *length_p)) {
            AbortError("invalid LENGTH packet");
            return false;
        }

        break;

    case WAS_COMMAND_STOP:
    case WAS_COMMAND_PREMATURE:
        // XXX
        AbortError(StringFormat<64>("unexpected packet: %d", cmd));
        return false;
    }

    return true;
}

void
WasServer::OnWasControlError(std::exception_ptr ep)
{
    assert(!control.IsDefined());

    AbortError(ep);
}

/*
 * constructor
 *
 */

WasServer *
was_server_new(struct pool &pool, EventLoop &event_loop,
               int control_fd, int input_fd, int output_fd,
               WasServerHandler &handler)
{
    assert(control_fd >= 0);
    assert(input_fd >= 0);
    assert(output_fd >= 0);

    return NewFromPool<WasServer>(pool, pool, event_loop,
                                  control_fd, input_fd,
                                  FileDescriptor(output_fd),
                                  handler);
}

void
was_server_free(WasServer *server)
{
    server->ReleaseError("shutting down WAS connection");
}

void
was_server_response(WasServer &server, http_status_t status,
                    StringMap &&headers, Istream *body)
{
    assert(server.request.pool != nullptr);
    assert(server.request.headers == nullptr);
    assert(server.response.body == nullptr);
    assert(http_status_is_valid(status));
    assert(!http_status_is_empty(status) || body == nullptr);

    server.control.BulkOn();

    if (!server.control.Send(WAS_COMMAND_STATUS, &status, sizeof(status)))
        return;

    if (body != nullptr && http_method_is_empty(server.request.method)) {
        if (server.request.method == HTTP_METHOD_HEAD) {
            off_t available = body->GetAvailable(false);
            if (available >= 0)
                headers.Set("content-length",
                            p_sprintf(server.request.pool, "%lu",
                                      (unsigned long)available));
        }

        body->CloseUnused();
        body = nullptr;
    }

    server.control.SendStrmap(WAS_COMMAND_HEADER, headers);

    if (body != nullptr) {
        server.response.body = was_output_new(*server.request.pool,
                                              server.control.GetEventLoop(),
                                              server.output_fd, *body,
                                              server);
        if (!server.control.SendEmpty(WAS_COMMAND_DATA) ||
            !was_output_check_length(*server.response.body))
            return;
    } else {
        if (!server.control.SendEmpty(WAS_COMMAND_NO_DATA))
            return;
    }

    server.control.BulkOff();
}
