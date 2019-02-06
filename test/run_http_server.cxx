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
#include "istream/ByteIstream.hxx"
#include "istream/DelayedIstream.hxx"
#include "istream/HeadIstream.hxx"
#include "istream/istream_hold.hxx"
#include "istream/istream_memory.hxx"
#include "istream/ZeroIstream.hxx"
#include "istream/istream.hxx"
#include "istream/UnusedHoldPtr.hxx"
#include "PInstance.hxx"
#include "pool/pool.hxx"
#include "event/TimerEvent.hxx"
#include "event/ShutdownListener.hxx"
#include "event/net/TemplateServerSocket.hxx"
#include "fb_pool.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "util/Cancellable.hxx"
#include "util/PrintException.hxx"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Instance;

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
};

class Connection final
    : public boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::auto_unlink>>,
    HttpServerConnectionHandler, Cancellable
{
    Instance &instance;

    PoolPtr pool;

    HttpServerConnection *connection;

    UnusedHoldIstreamPtr request_body;

    TimerEvent timer;

public:
    Connection(Instance &instance,
               UniqueSocketDescriptor &&_fd, SocketAddress address) noexcept;

    ~Connection() noexcept {
        if (connection != nullptr)
            http_server_connection_close(connection);
    }

private:
    void OnTimer() noexcept;

    /* virtual methods from class Cancellable */
    void Cancel() noexcept override {
        request_body.Clear();
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

using Listener = TemplateServerSocket<Connection, Instance &>;

struct Instance final : PInstance {
    ShutdownListener shutdown_listener;

    Mode mode;

    std::unique_ptr<Listener> listener;

    Instance()
        :shutdown_listener(event_loop, BIND_THIS_METHOD(ShutdownCallback)) {}

    void OnConnectionClosed() noexcept;

private:
    void ShutdownCallback() noexcept;
};

Connection::Connection(Instance &_instance,
                       UniqueSocketDescriptor &&fd,
                       SocketAddress address) noexcept
    :instance(_instance),
     pool(PoolPtr::Donate(),
          *pool_new_linear(instance.root_pool, "connection", 2048)),
     connection(http_server_connection_new(pool, instance.event_loop,
                                           fd.Release(), FdType::FD_SOCKET,
                                           nullptr,
                                           nullptr,
                                           address,
                                           true,
                                           *this)),
     timer(instance.event_loop, BIND_THIS_METHOD(OnTimer)) {}


void
Instance::ShutdownCallback() noexcept
{
    listener.reset();
}

void
Connection::OnTimer() noexcept
{
    instance.OnConnectionClosed();
    delete this;
}

void
Instance::OnConnectionClosed() noexcept
{
    if (!listener)
        shutdown_listener.Disable();
}

/*
 * http_server handler
 *
 */

void
Connection::HandleHttpRequest(HttpServerRequest &request,
                              gcc_unused CancellablePointer &cancel_ptr)
{
    switch (instance.mode) {
        http_status_t status;
        static char data[0x100];

    case Mode::MODE_NULL:
        if (request.body)
            sink_null_new(request.pool, std::move(request.body));

        http_server_response(&request, HTTP_STATUS_NO_CONTENT,
                             HttpHeaders(request.pool), nullptr);
        break;

    case Mode::MIRROR:
        status = request.body
            ? HTTP_STATUS_OK
            : HTTP_STATUS_NO_CONTENT;
        http_server_response(&request, status,
                             HttpHeaders(request.pool),
                             std::move(request.body));
        break;

    case Mode::CLOSE:
        /* disable keep-alive */
        http_server_connection_graceful(&request.connection);

        /* fall through */

    case Mode::DUMMY:
        if (request.body)
            sink_null_new(request.pool, std::move(request.body));

        {
            auto body = istream_head_new(request.pool,
                                         istream_zero_new(request.pool),
                                         256, false);
            body = istream_byte_new(request.pool, std::move(body));

            http_server_response(&request, HTTP_STATUS_OK,
                                 HttpHeaders(request.pool),
                                 std::move(body));
        }

        break;

    case Mode::FIXED:
        if (request.body)
            sink_null_new(request.pool, std::move(request.body));

        http_server_response(&request, HTTP_STATUS_OK, HttpHeaders(request.pool),
                             istream_memory_new(request.pool,
                                                data, sizeof(data)));
        break;

    case Mode::HUGE_:
        if (request.body)
            sink_null_new(request.pool, std::move(request.body));

        http_server_response(&request, HTTP_STATUS_OK,
                             HttpHeaders(request.pool),
                             istream_head_new(request.pool,
                                              istream_zero_new(request.pool),
                                              512 * 1024, true));
        break;

    case Mode::HOLD:
        request_body = UnusedHoldIstreamPtr(request.pool,
                                            std::move(request.body));

        {
            auto delayed = istream_delayed_new(request.pool,
                                               instance.event_loop);
            delayed.second.cancel_ptr = *this;

            http_server_response(&request, HTTP_STATUS_OK,
                                 HttpHeaders(request.pool),
                                 std::move(delayed.first));
        }

        static constexpr struct timeval t{0,0};
        timer.Add(t);
        break;
    }
}

void
Connection::HttpConnectionError(std::exception_ptr e)
{
    PrintException(e);

    connection = nullptr;

    instance.OnConnectionClosed();
    delete this;
}

void
Connection::HttpConnectionClosed()
{
    connection = nullptr;
    instance.OnConnectionClosed();
    delete this;
}

/*
 * main
 *
 */

int
main(int argc, char **argv)
try {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s INFD OUTFD {null|mirror|close|dummy|fixed|huge|hold}\n", argv[0]);
        return EXIT_FAILURE;
    }

    UniqueSocketDescriptor listen_fd;
    int in_fd = -1, out_fd = -1;

    if (strcmp(argv[1], "accept") == 0) {
        listen_fd = UniqueSocketDescriptor(atoi(argv[2]));
    } else {
        in_fd = atoi(argv[1]);
        out_fd = atoi(argv[2]);
    }

    direct_global_init();
    const ScopeFbPoolInit fb_pool_init;

    Instance instance;
    instance.shutdown_listener.Enable();

    const char *mode = argv[3];
    if (strcmp(mode, "null") == 0)
        instance.mode = Mode::MODE_NULL;
    else if (strcmp(mode, "mirror") == 0)
        instance.mode = Mode::MIRROR;
    else if (strcmp(mode, "close") == 0)
        instance.mode = Mode::CLOSE;
    else if (strcmp(mode, "dummy") == 0)
        instance.mode = Mode::DUMMY;
    else if (strcmp(mode, "fixed") == 0)
        instance.mode = Mode::FIXED;
    else if (strcmp(mode, "huge") == 0)
        instance.mode = Mode::HUGE_;
    else if (strcmp(mode, "hold") == 0)
        instance.mode = Mode::HOLD;
    else {
        fprintf(stderr, "Unknown mode: %s\n", mode);
        return EXIT_FAILURE;
    }

    if (listen_fd.IsDefined()) {
        instance.listener = std::make_unique<Listener>(instance.event_loop,
                                                       instance);
        instance.listener->Listen(std::move(listen_fd));
    } else {
        UniqueSocketDescriptor sockfd;
        if (in_fd != out_fd) {
            sockfd = duplex_new(instance.event_loop, instance.root_pool,
                                UniqueFileDescriptor(in_fd),
                                UniqueFileDescriptor(out_fd));
        } else
            sockfd = UniqueSocketDescriptor(in_fd);

        new Connection(instance, std::move(sockfd), {});
    }

    instance.event_loop.Dispatch();

    return EXIT_SUCCESS;
} catch (...) {
    PrintException(std::current_exception());
    return EXIT_FAILURE;
}
