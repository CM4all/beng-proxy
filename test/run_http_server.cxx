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

#include "http_server/http_server.hxx"
#include "http_server/Request.hxx"
#include "http_server/Handler.hxx"
#include "http_headers.hxx"
#include "duplex.hxx"
#include "direct.hxx"
#include "istream/sink_null.hxx"
#include "istream/istream_byte.hxx"
#include "istream/istream_delayed.hxx"
#include "istream/istream_head.hxx"
#include "istream/istream_hold.hxx"
#include "istream/istream_memory.hxx"
#include "istream/istream_zero.hxx"
#include "istream/istream.hxx"
#include "PInstance.hxx"
#include "pool.hxx"
#include "event/TimerEvent.hxx"
#include "event/ShutdownListener.hxx"
#include "fb_pool.hxx"
#include "net/SocketDescriptor.hxx"
#include "util/Cancellable.hxx"
#include "util/PrintException.hxx"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Instance final : PInstance, HttpServerConnectionHandler, Cancellable {
    ShutdownListener shutdown_listener;

    enum class Mode {
        MODE_NULL,
        MIRROR,

        /**
         * Response body of unknown length with keep-alive disabled.
         * Response body ends when socket is closed.
         */
        CLOSE,

        DUMMY,
        FIXED,
        HUGE_,
        HOLD,
    } mode;

    HttpServerConnection *connection;

    Istream *request_body;

    TimerEvent timer;

    Instance()
        :shutdown_listener(event_loop, BIND_THIS_METHOD(ShutdownCallback)),
         timer(event_loop, BIND_THIS_METHOD(OnTimer)) {}

    void ShutdownCallback();

    void OnTimer();

    /* virtual methods from class Cancellable */
    void Cancel() override {
        if (request_body != nullptr)
            request_body->CloseUnused();

        timer.Cancel();
    }

    /* virtual methods from class HttpServerConnectionHandler */
    void HandleHttpRequest(HttpServerRequest &request,
                           CancellablePointer &cancel_ptr) override;

    void LogHttpRequest(HttpServerRequest &,
                        http_status_t, int64_t,
                        uint64_t, uint64_t) override {}

    void HttpConnectionError(std::exception_ptr e) override;
    void HttpConnectionClosed() override;
};

void
Instance::ShutdownCallback()
{
    http_server_connection_close(connection);
}

void
Instance::OnTimer()
{
    http_server_connection_close(connection);
    shutdown_listener.Disable();
}

/*
 * http_server handler
 *
 */

void
Instance::HandleHttpRequest(HttpServerRequest &request,
                            gcc_unused CancellablePointer &cancel_ptr)
{
    switch (mode) {
        Istream *body;
        static char data[0x100];

    case Instance::Mode::MODE_NULL:
        if (request.body != nullptr)
            sink_null_new(request.pool, *request.body);

        http_server_response(&request, HTTP_STATUS_NO_CONTENT,
                             HttpHeaders(request.pool), nullptr);
        break;

    case Instance::Mode::MIRROR:
        http_server_response(&request,
                             request.body == nullptr
                             ? HTTP_STATUS_NO_CONTENT : HTTP_STATUS_OK,
                             HttpHeaders(request.pool),
                             request.body);
        break;

    case Instance::Mode::CLOSE:
        /* disable keep-alive */
        http_server_connection_graceful(&request.connection);

        /* fall through */

    case Instance::Mode::DUMMY:
        if (request.body != nullptr)
            sink_null_new(request.pool, *request.body);

        body = istream_head_new(&request.pool,
                                *istream_zero_new(&request.pool),
                                256, false);
        body = istream_byte_new(request.pool, *body);

        http_server_response(&request, HTTP_STATUS_OK,
                             HttpHeaders(request.pool), body);
        break;

    case Instance::Mode::FIXED:
        if (request.body != nullptr)
            sink_null_new(request.pool, *request.body);

        http_server_response(&request, HTTP_STATUS_OK, HttpHeaders(request.pool),
                             istream_memory_new(&request.pool, data, sizeof(data)));
        break;

    case Instance::Mode::HUGE_:
        if (request.body != nullptr)
            sink_null_new(request.pool, *request.body);

        http_server_response(&request, HTTP_STATUS_OK,
                             HttpHeaders(request.pool),
                             istream_head_new(&request.pool,
                                              *istream_zero_new(&request.pool),
                                              512 * 1024, true));
        break;

    case Instance::Mode::HOLD:
        request_body = request.body != nullptr
            ? istream_hold_new(request.pool, *request.body)
            : nullptr;

        body = istream_delayed_new(&request.pool);
        istream_delayed_cancellable_ptr(*body) = *this;

        http_server_response(&request, HTTP_STATUS_OK,
                             HttpHeaders(request.pool), body);

        static constexpr struct timeval t{0,0};
        timer.Add(t);
        break;
    }
}

void
Instance::HttpConnectionError(std::exception_ptr e)
{
    timer.Cancel();
    shutdown_listener.Disable();

    PrintException(e);
}

void
Instance::HttpConnectionClosed()
{
    timer.Cancel();
    shutdown_listener.Disable();
}

/*
 * main
 *
 */

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s INFD OUTFD {null|mirror|close|dummy|fixed|huge|hold}\n", argv[0]);
        return EXIT_FAILURE;
    }

    int in_fd, out_fd;

    if (strcmp(argv[1], "accept") == 0) {
        const int listen_fd = atoi(argv[2]);
        in_fd = out_fd = accept(listen_fd, nullptr, 0);
        if (in_fd < 0) {
            perror("accept() failed");
            return EXIT_FAILURE;
        }
    } else {
        in_fd = atoi(argv[1]);
        out_fd = atoi(argv[2]);
    }

    direct_global_init();
    const ScopeFbPoolInit fb_pool_init;

    Instance instance;
    instance.shutdown_listener.Enable();

    int sockfd;
    if (in_fd != out_fd) {
        sockfd = duplex_new(instance.event_loop, instance.root_pool,
                            in_fd, out_fd);
        if (sockfd < 0) {
            perror("duplex_new() failed");
            exit(2);
        }
    } else
        sockfd = in_fd;

    const char *mode = argv[3];
    if (strcmp(mode, "null") == 0)
        instance.mode = Instance::Mode::MODE_NULL;
    else if (strcmp(mode, "mirror") == 0)
        instance.mode = Instance::Mode::MIRROR;
    else if (strcmp(mode, "close") == 0)
        instance.mode = Instance::Mode::CLOSE;
    else if (strcmp(mode, "dummy") == 0)
        instance.mode = Instance::Mode::DUMMY;
    else if (strcmp(mode, "fixed") == 0)
        instance.mode = Instance::Mode::FIXED;
    else if (strcmp(mode, "huge") == 0)
        instance.mode = Instance::Mode::HUGE_;
    else if (strcmp(mode, "hold") == 0)
        instance.mode = Instance::Mode::HOLD;
    else {
        fprintf(stderr, "Unknown mode: %s\n", mode);
        return EXIT_FAILURE;
    }

    instance.connection = http_server_connection_new(instance.root_pool,
                                                     instance.event_loop,
                                                     SocketDescriptor::FromFileDescriptor(FileDescriptor(sockfd)),
                                                     FdType::FD_SOCKET,
                                                     nullptr, nullptr,
                                                     nullptr, nullptr,
                                                     true, instance);

    instance.event_loop.Dispatch();

    return EXIT_SUCCESS;
}
