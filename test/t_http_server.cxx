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
#include "direct.hxx"
#include "PInstance.hxx"
#include "pool.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/istream_catch.hxx"
#include "fb_pool.hxx"
#include "net/SocketDescriptor.hxx"
#include "io/FileDescriptor.hxx"
#include "util/PrintException.hxx"

#include <stdio.h>
#include <stdlib.h>

struct Instance final : HttpServerConnectionHandler {
    struct pool *pool;

    HttpServerConnection *connection = nullptr;

    explicit Instance(struct pool &_pool)
        :pool(pool_new_libc(&_pool, "catch")) {}

    ~Instance() {
        CheckCloseConnection();
    }

    void CloseConnection() {
        http_server_connection_close(connection);
        connection = nullptr;
    }

    void CheckCloseConnection() {
        if (connection != nullptr)
            CloseConnection();
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

static std::exception_ptr
catch_callback(std::exception_ptr ep, gcc_unused void *ctx)
{
    PrintException(ep);
    return {};
}

void
Instance::HandleHttpRequest(HttpServerRequest &request,
                            gcc_unused CancellablePointer &cancel_ptr)
{
    http_server_response(&request, HTTP_STATUS_OK, HttpHeaders(request.pool),
                         istream_catch_new(&request.pool,
                                           std::move(request.body),
                                           catch_callback, nullptr));

    CloseConnection();
}

void
Instance::HttpConnectionError(std::exception_ptr e)
{
    connection = nullptr;

    PrintException(e);
}

void
Instance::HttpConnectionClosed()
{
    connection = nullptr;
}

static void
test_catch(EventLoop &event_loop, struct pool *_pool)
{
    SocketDescriptor client_socket, server_socket;
    if (!SocketDescriptor::CreateSocketPair(AF_LOCAL, SOCK_STREAM, 0,
                                            client_socket, server_socket)) {
        perror("socketpair() failed");
        exit(EXIT_FAILURE);
    }

    static constexpr char request[] =
        "POST / HTTP/1.1\r\nContent-Length: 1024\r\n\r\nfoo";
    send(client_socket.Get(), request, sizeof(request) - 1, 0);

    Instance instance(*_pool);
    instance.connection =
        http_server_connection_new(instance.pool, event_loop,
                                   server_socket, FdType::FD_SOCKET,
                                   nullptr, nullptr,
                                   nullptr, nullptr,
                                   true, instance);
    pool_unref(instance.pool);

    event_loop.Dispatch();

    client_socket.Close();
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    direct_global_init();
    const ScopeFbPoolInit fb_pool_init;
    PInstance instance;

    test_catch(instance.event_loop, instance.root_pool);
}
