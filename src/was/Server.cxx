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
#include "direct.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/istream.hxx"
#include "istream/istream_null.hxx"
#include "strmap.hxx"
#include "pool/pool.hxx"
#include "io/FileDescriptor.hxx"
#include "util/ConstBuffer.hxx"
#include "util/StringFormat.hxx"

#include <was/protocol.h>

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

class WasServer final : WasControlHandler, WasOutputHandler, WasInputHandler {
    struct pool &pool;

    const int control_fd, input_fd;
    FileDescriptor output_fd;

    WasControl control;

    WasServerHandler &handler;

    struct Request {
        struct pool *pool;

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

        enum class State : uint8_t {
            /**
             * No request is being processed currently.
             */
            NONE,

            /**
             * Receiving headers.
             */
            HEADERS,

            /**
             * Receiving headers.
             */
            PENDING,

            /**
             * Request metadata already submitted to
             * WasServerHandler::OnWasRequest().
             */
            SUBMITTED,
        } state = State::NONE;
    } request;

    struct {
        http_status_t status;

        WasOutput *body;
    } response;

public:
    WasServer(struct pool &_pool, EventLoop &event_loop,
              int _control_fd, int _input_fd, FileDescriptor _output_fd,
              WasServerHandler &_handler)
        :pool(_pool),
         control_fd(_control_fd), input_fd(_input_fd), output_fd(_output_fd),
         control(event_loop, control_fd, *this),
         handler(_handler) {}

    void Free() {
        ReleaseError("shutting down WAS connection");
    }

    void SendResponse(http_status_t status,
                      StringMap &&headers, UnusedIstreamPtr body) noexcept;

private:
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
        auto &handler2 = handler;
        ReleaseError(ep);
        handler2.OnWasClosed();
    }

    void AbortError(const char *msg) {
        AbortError(std::make_exception_ptr(WasProtocolError(msg)));
    }

    /**
     * Abort receiving the response status/headers from the WAS server.
     */
    void AbortUnused() {
        auto &handler2 = handler;
        ReleaseUnused();
        handler2.OnWasClosed();
    }

    /* virtual methods from class WasControlHandler */
    bool OnWasControlPacket(enum was_command cmd,
                            ConstBuffer<void> payload) noexcept override;

    bool OnWasControlDrained() noexcept override {
        if (request.state == Request::State::PENDING) {
            request.state = Request::State::SUBMITTED;

            UnusedIstreamPtr body;
            if (request.released) {
                was_input_free_unused(request.body);
                request.body = nullptr;

                body = istream_null_new(*request.pool);
            } else if (request.body != nullptr)
                body = was_input_enable(*request.body);

            handler.OnWasRequest(*request.pool, request.method,
                                 request.uri, std::move(*request.headers),
                                 std::move(body));
            /* XXX check if connection has been closed */
        }

        return true;
    }

    void OnWasControlDone() noexcept override {
        assert(!control.IsDefined());
    }

    void OnWasControlError(std::exception_ptr ep) noexcept override;

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

    if (request.state != Request::State::NONE) {
        if (request.body != nullptr)
            was_input_free_p(&request.body, ep);

        if (request.state == Request::State::SUBMITTED &&
            response.body != nullptr)
            was_output_free_p(&response.body);

        pool_unref(request.pool);
    }

    CloseFiles();

    this->~WasServer();
}

void
WasServer::ReleaseUnused()
{
    if (control.IsDefined())
        control.ReleaseSocket();

    if (request.state != Request::State::NONE) {
        if (request.body != nullptr)
            was_input_free_unused_p(&request.body);

        if (request.state == Request::State::SUBMITTED &&
            response.body != nullptr)
            was_output_free_p(&response.body);

        pool_unref(request.pool);
    }

    CloseFiles();

    this->~WasServer();
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

    assert(request.state == Request::State::SUBMITTED);
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
    assert(request.state == Request::State::SUBMITTED);
    assert(request.body != nullptr);
    assert(request.released);

    request.body = nullptr;

    // TODO
}

void
WasServer::WasInputError()
{
    assert(request.state == Request::State::SUBMITTED);
    assert(request.body != nullptr);

    request.body = nullptr;

    AbortUnused();
}

/*
 * Control channel handler
 */

bool
WasServer::OnWasControlPacket(enum was_command cmd,
                              ConstBuffer<void> payload) noexcept
{
    switch (cmd) {
        const uint64_t *length_p;
        const char *p;
        http_method_t method;

    case WAS_COMMAND_NOP:
        break;

    case WAS_COMMAND_REQUEST:
        if (request.state != Request::State::NONE) {
            AbortError("misplaced REQUEST packet");
            return false;
        }

        request.pool = pool_new_linear(&pool, "was_server_request", 32768);
        request.method = HTTP_METHOD_GET;
        request.uri = nullptr;
        request.headers = strmap_new(request.pool);
        request.body = nullptr;
        request.state = Request::State::HEADERS;
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
        if (request.state != Request::State::HEADERS ||
            request.uri != nullptr) {
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
        if (request.state != Request::State::HEADERS) {
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
        if (request.state != Request::State::HEADERS ||
            request.uri == nullptr) {
            AbortError("misplaced NO_DATA packet");
            return false;
        }

        request.body = nullptr;
        request.state = Request::State::PENDING;
        break;

    case WAS_COMMAND_DATA:
        if (request.state != Request::State::HEADERS ||
            request.uri == nullptr) {
            AbortError("misplaced DATA packet");
            return false;
        }

        request.body = was_input_new(*request.pool, control.GetEventLoop(),
                                     input_fd, *this);
        request.state = Request::State::PENDING;
        break;

    case WAS_COMMAND_LENGTH:
        if (request.state < Request::State::PENDING ||
            request.body == nullptr) {
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
WasServer::OnWasControlError(std::exception_ptr ep) noexcept
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
    server->Free();
}

inline void
WasServer::SendResponse(http_status_t status,
                        StringMap &&headers, UnusedIstreamPtr body) noexcept
{
    assert(request.state == Request::State::SUBMITTED);
    assert(response.body == nullptr);
    assert(http_status_is_valid(status));
    assert(!http_status_is_empty(status) || !body);

    control.BulkOn();

    if (!control.Send(WAS_COMMAND_STATUS, &status, sizeof(status)))
        return;

    if (body && http_method_is_empty(request.method)) {
        if (request.method == HTTP_METHOD_HEAD) {
            off_t available = body.GetAvailable(false);
            if (available >= 0)
                headers.Set("content-length",
                            p_sprintf(request.pool, "%lu",
                                      (unsigned long)available));
        }

        body.Clear();
    }

    control.SendStrmap(WAS_COMMAND_HEADER, headers);

    if (body) {
        response.body = was_output_new(*request.pool,
                                       control.GetEventLoop(),
                                       output_fd, std::move(body),
                                       *this);
        if (!control.SendEmpty(WAS_COMMAND_DATA) ||
            !was_output_check_length(*response.body))
            return;
    } else {
        if (!control.SendEmpty(WAS_COMMAND_NO_DATA))
            return;
    }

    control.BulkOff();
}

void
was_server_response(WasServer &server, http_status_t status,
                    StringMap &&headers, UnusedIstreamPtr body) noexcept
{
    server.SendResponse(status, std::move(headers), std::move(body));
}
