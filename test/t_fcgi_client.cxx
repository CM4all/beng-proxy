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

#define ENABLE_PREMATURE_CLOSE_HEADERS
#define ENABLE_PREMATURE_CLOSE_BODY
#define USE_BUCKETS
#define ENABLE_HUGE_BODY
#define ENABLE_PREMATURE_END
#define ENABLE_EXCESS_DATA

#include "t_client.hxx"
#include "tio.hxx"
#include "fcgi/Client.hxx"
#include "system/SetupProcess.hxx"
#include "io/FileDescriptor.hxx"
#include "lease.hxx"
#include "direct.hxx"
#include "istream/UnusedPtr.hxx"
#include "strmap.hxx"
#include "fb_pool.hxx"
#include "net/SocketDescriptor.hxx"
#include "util/ConstBuffer.hxx"
#include "util/ByteOrder.hxx"
#include "fcgi_server.hxx"

#include "util/Compiler.h"

#include <sys/wait.h>

static void
mirror_data(size_t length)
{
    char buffer[4096];

    while (length > 0) {
        size_t l = length;
        if (l > sizeof(buffer))
            l = sizeof(buffer);

        ssize_t nbytes = recv(0, buffer, l, MSG_WAITALL);
        if (nbytes <= 0)
            _exit(EXIT_FAILURE);

        write_full(buffer, nbytes);
        length -= nbytes;
    }
}

static void
fcgi_server_mirror(struct pool *pool)
{
    FcgiRequest request;
    read_fcgi_request(pool, &request);

    http_status_t status = request.length == 0
        ? HTTP_STATUS_NO_CONTENT
        : HTTP_STATUS_OK;

    if (request.length > 0) {
        char buffer[32];
        sprintf(buffer, "%llu", (unsigned long long)request.length);
        request.headers->Add("content-length", buffer);
    }

    write_fcgi_headers(&request, status, request.headers);

    if (request.method == HTTP_METHOD_HEAD)
        discard_fcgi_request_body(&request);
    else {
        while (true) {
            struct fcgi_record_header header;
            read_fcgi_header(&header);

            if (header.type != FCGI_STDIN ||
                header.request_id != request.id)
                abort();

            if (header.content_length == 0)
                break;

            header.type = FCGI_STDOUT;
            write_full(&header, sizeof(header));
            mirror_data(FromBE16(header.content_length) + header.padding_length);
        }
    }

    write_fcgi_end(&request);
}

static void
fcgi_server_null(struct pool *pool)
{
    FcgiRequest request;
    read_fcgi_request(pool, &request);
    write_fcgi_headers(&request, HTTP_STATUS_NO_CONTENT, nullptr);
    write_fcgi_end(&request);
    discard_fcgi_request_body(&request);
}

static void
fcgi_server_hello(struct pool *pool)
{
    FcgiRequest request;
    read_fcgi_request(pool, &request);

    write_fcgi_headers(&request, HTTP_STATUS_OK, nullptr);
    discard_fcgi_request_body(&request);
    write_fcgi_stdout_string(&request, "hello");
    write_fcgi_end(&request);
}

static void
fcgi_server_tiny(struct pool *pool)
{
    FcgiRequest request;
    read_fcgi_request(pool, &request);

    discard_fcgi_request_body(&request);
    write_fcgi_stdout_string(&request, "content-length: 5\n\nhello");
    write_fcgi_end(&request);
}

static void
fcgi_server_huge(struct pool *pool)
{
    FcgiRequest request;
    read_fcgi_request(pool, &request);

    discard_fcgi_request_body(&request);
    write_fcgi_stdout_string(&request, "content-length: 524288\n\nhello");

    char buffer[23456];
    memset(buffer, 0xab, sizeof(buffer));

    size_t remaining = 524288;
    while (remaining > 0) {
        size_t nbytes = std::min(remaining, sizeof(buffer));
        write_fcgi_stdout(&request, buffer, nbytes);
        remaining -= nbytes;
    }

    write_fcgi_end(&request);
}

static void
fcgi_server_hold(struct pool *pool)
{
    FcgiRequest request;
    read_fcgi_request(pool, &request);
    write_fcgi_headers(&request, HTTP_STATUS_OK, nullptr);

    /* wait until the connection gets closed */
    struct fcgi_record_header header;
    read_fcgi_header(&header);
}

static void
fcgi_server_premature_close_headers(struct pool *pool)
{
    FcgiRequest request;
    read_fcgi_request(pool, &request);
    discard_fcgi_request_body(&request);

    const struct fcgi_record_header header = {
        .version = FCGI_VERSION_1,
        .type = FCGI_STDOUT,
        .request_id = request.id,
        .content_length = ToBE16(1024),
        .padding_length = 0,
        .reserved = 0,
    };

    write_full(&header, sizeof(header));

    const char *data = "Foo: 1\nBar: 1\nX: ";
    write_full(data, strlen(data));
}

static void
fcgi_server_premature_close_body(struct pool *pool)
{
    FcgiRequest request;
    read_fcgi_request(pool, &request);
    discard_fcgi_request_body(&request);

    const struct fcgi_record_header header = {
        .version = FCGI_VERSION_1,
        .type = FCGI_STDOUT,
        .request_id = request.id,
        .content_length = ToBE16(1024),
        .padding_length = 0,
        .reserved = 0,
    };

    write_full(&header, sizeof(header));

    const char *data = "Foo: 1\nBar: 1\n\nFoo Bar";
    write_full(data, strlen(data));
}

static void
fcgi_server_premature_end(struct pool *pool)
{
    FcgiRequest request;
    read_fcgi_request(pool, &request);

    discard_fcgi_request_body(&request);
    write_fcgi_stdout_string(&request, "content-length: 524288\n\nhello");
    write_fcgi_end(&request);
}

static void
fcgi_server_excess_data(struct pool *pool)
{
    FcgiRequest request;
    read_fcgi_request(pool, &request);

    discard_fcgi_request_body(&request);
    write_fcgi_stdout_string(&request, "content-length: 5\n\nhello world");
    write_fcgi_end(&request);
}

struct Connection {
    EventLoop &event_loop;
    const pid_t pid;
    SocketDescriptor fd;

    Connection(EventLoop &_event_loop, pid_t _pid, SocketDescriptor _fd)
        :event_loop(_event_loop), pid(_pid), fd(_fd) {}
    static Connection *New(EventLoop &event_loop,
                           void (*f)(struct pool *pool));

    ~Connection();

    void Request(struct pool *pool,
                 Lease &lease,
                 http_method_t method, const char *uri,
                 StringMap &&headers, UnusedIstreamPtr body,
                 HttpResponseHandler &handler,
                 CancellablePointer &cancel_ptr) {
        fcgi_client_request(pool, event_loop, fd, FdType::FD_SOCKET,
                            lease,
                            method, uri, uri, nullptr, nullptr, nullptr,
                            nullptr, "192.168.1.100",
                            headers, std::move(body),
                            nullptr,
                            -1,
                            handler, cancel_ptr);
    }

    static Connection *NewMirror(struct pool &, EventLoop &event_loop) {
        return New(event_loop, fcgi_server_mirror);
    }

    static Connection *NewNull(struct pool &, EventLoop &event_loop) {
        return New(event_loop, fcgi_server_null);
    }

    static Connection *NewDummy(struct pool &, EventLoop &event_loop) {
        return New(event_loop, fcgi_server_hello);
    }

    static Connection *NewFixed(struct pool &, EventLoop &event_loop) {
        return New(event_loop, fcgi_server_hello);
    }

    static Connection *NewTiny(struct pool &, EventLoop &event_loop) {
        return New(event_loop, fcgi_server_tiny);
    }

    static Connection *NewHuge(struct pool &, EventLoop &event_loop) {
        return New(event_loop, fcgi_server_huge);
    }

    static Connection *NewHold(struct pool &, EventLoop &event_loop) {
        return New(event_loop, fcgi_server_hold);
    }

    static Connection *NewPrematureCloseHeaders(struct pool &,
                                                EventLoop &event_loop) {
        return New(event_loop, fcgi_server_premature_close_headers);
    }

    static Connection *NewPrematureCloseBody(struct pool &,
                                             EventLoop &event_loop) {
        return New(event_loop, fcgi_server_premature_close_body);
    }

    static Connection *NewPrematureEnd(struct pool &, EventLoop &event_loop) {
        return New(event_loop, fcgi_server_premature_end);
    }

    static Connection *NewExcessData(struct pool &, EventLoop &event_loop) {
        return New(event_loop, fcgi_server_excess_data);
    }
};

Connection *
Connection::New(EventLoop &event_loop, void (*f)(struct pool *pool))
{
    SocketDescriptor server_socket, client_socket;
    if (!SocketDescriptor::CreateSocketPair(AF_LOCAL, SOCK_STREAM, 0,
                                            server_socket, client_socket)) {
        perror("socketpair() failed");
        exit(EXIT_FAILURE);
    }

    const auto pid = fork();
    if (pid < 0) {
        perror("fork() failed");
        abort();
    }

    if (pid == 0) {
        server_socket.Duplicate(FileDescriptor(STDIN_FILENO));
        server_socket.Duplicate(FileDescriptor(STDOUT_FILENO));
        server_socket.Close();
        client_socket.Close();

        struct pool *pool = pool_new_libc(nullptr, "f");
        f(pool);
        shutdown(0, SHUT_RDWR);
        pool_unref(pool);
        _exit(EXIT_SUCCESS);
    }

    server_socket.Close();
    client_socket.SetNonBlocking();
    return new Connection(event_loop, pid, client_socket);
}

Connection::~Connection()
{
    assert(pid >= 1);
    assert(fd.IsDefined());

    fd.Close();

    int status;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid() failed");
        abort();
    }

    assert(!WIFSIGNALED(status));
}

/*
 * main
 *
 */

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    SetupProcess();
    direct_global_init();
    const ScopeFbPoolInit fb_pool_init;

    run_all_tests<Connection>();

    int status;
    while (wait(&status) > 0) {
        assert(!WIFSIGNALED(status));
    }
}
