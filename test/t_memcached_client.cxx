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

#include "memcached/memcached_client.hxx"
#include "HttpResponseHandler.hxx"
#include "system/SetupProcess.hxx"
#include "io/FileDescriptor.hxx"
#include "http/HeaderWriter.hxx"
#include "lease.hxx"
#include "direct.hxx"
#include "istream/Handler.hxx"
#include "istream/Pointer.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/New.hxx"
#include "fb_pool.hxx"
#include "pool/pool.hxx"
#include "PInstance.hxx"
#include "net/SocketDescriptor.hxx"
#include "util/Cast.hxx"
#include "util/Cancellable.hxx"

#include <stdexcept>

#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

static SocketDescriptor
connect_fake_server()
{
    SocketDescriptor client_socket, server_socket;
    if (!SocketDescriptor::CreateSocketPair(AF_LOCAL, SOCK_STREAM, 0,
                                            client_socket, server_socket)) {
        perror("socketpair() failed");
        exit(EXIT_FAILURE);
    }

    const auto pid = fork();
    if (pid < 0) {
        perror("fork() failed");
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        server_socket.CheckDuplicate(FileDescriptor(STDIN_FILENO));
        server_socket.CheckDuplicate(FileDescriptor(STDOUT_FILENO));

        execl("./test/fake_memcached_server", "fake_memcached_server",
              nullptr);
        perror("exec() failed");
        exit(EXIT_FAILURE);
    }

    server_socket.Close();
    client_socket.SetNonBlocking();
    return client_socket;
}

struct Context final : PInstance, Lease, IstreamHandler {
    struct pool *pool;

    unsigned data_blocking = 0;
    bool close_value_early = false;
    bool close_value_late = false;
    bool close_value_data = false;
    CancellablePointer cancel_ptr;
    SocketDescriptor fd = SocketDescriptor::Undefined();
    bool released = false, reuse = false, got_response = false;
    enum memcached_response_status status;

    IstreamPointer value;
    off_t value_data = 0, consumed_value_data = 0;
    bool value_eof = false, value_abort = false, value_closed = false;

    Context():value(nullptr) {}

    /* virtual methods from class IstreamHandler */
    size_t OnData(const void *data, size_t length) noexcept override;
    void OnEof() noexcept override;
    void OnError(std::exception_ptr ep) noexcept override;

    /* virtual methods from class Lease */
    void ReleaseLease(bool _reuse) noexcept override {
        fd.Close();
        released = true;
        reuse = _reuse;
    }
};

/*
 * request value istream
 *
 */

static char request_value[8192];

struct RequestValueIstream final : public Istream {
    CancellablePointer cancel_ptr;

    bool read_close, read_abort;

    size_t sent = 0;

    RequestValueIstream(struct pool &p, bool _read_close, bool _read_abort)
        :Istream(p), read_close(_read_close), read_abort(_read_abort) {}

    off_t _GetAvailable(gcc_unused bool partial) noexcept override {
        return sizeof(request_value) - sent;
    }

    void _Read() noexcept override;
};

void
RequestValueIstream::_Read() noexcept
{
    if (read_close) {
        DestroyError(std::make_exception_ptr(std::runtime_error("read_close")));
    } else if (read_abort)
        cancel_ptr.Cancel();
    else if (sent >= sizeof(request_value))
        DestroyEof();
    else {
        size_t nbytes = InvokeData(request_value + sent,
                                   sizeof(request_value) - sent);
        if (nbytes == 0)
            return;

        sent += nbytes;

        if (sent >= sizeof(request_value))
            DestroyEof();
    }
}

static Istream *
request_value_new(struct pool *pool, bool read_close, bool read_abort)
{
    return NewIstream<RequestValueIstream>(*pool, read_close, read_abort);
}

static CancellablePointer &
request_value_cancel_ptr(Istream &istream)
{
    auto &v = (RequestValueIstream &)istream;

    return v.cancel_ptr;
}

/*
 * istream handler
 *
 */

size_t
Context::OnData(gcc_unused const void *data, size_t length) noexcept
{
    value_data += length;

    if (close_value_data) {
        value_closed = true;
        value.ClearAndClose();
        return 0;
    }

    if (data_blocking) {
        --data_blocking;
        return 0;
    }

    consumed_value_data += length;
    return length;
}

void
Context::OnEof() noexcept
{
    value.Clear();
    value_eof = true;
}

void
Context::OnError(std::exception_ptr) noexcept
{
    value.Clear();
    value_abort = true;
}

/*
 * memcached_response_handler_t
 *
 */

static void
my_mcd_response(enum memcached_response_status status,
                gcc_unused const void *extras,
                gcc_unused size_t extras_length,
                gcc_unused const void *key,
                gcc_unused size_t key_length,
                UnusedIstreamPtr value, void *ctx)
{
    auto *c = (Context *)ctx;

    assert(!c->got_response);

    c->got_response = true;
    c->status = status;

    if (c->close_value_early)
        value.Clear();
    else if (value)
        c->value.Set(std::move(value), *c);

    if (c->close_value_late) {
        c->value_closed = true;
        c->value.ClearAndClose();
    }
}

static void
my_mcd_error(std::exception_ptr, gcc_unused void *ctx)
{
    auto *c = (Context *)ctx;

    assert(!c->got_response);

    c->got_response = true;
    c->status = (memcached_response_status)-1;
}

static const struct memcached_client_handler my_mcd_handler = {
    .response = my_mcd_response,
    .error = my_mcd_error,
};


/*
 * tests
 *
 */

static void
test_basic(struct pool *pool, Context *c)
{
    c->fd = connect_fake_server();

    memcached_client_invoke(pool, c->event_loop,
                            c->fd, FdType::FD_SOCKET, *c,
                            MEMCACHED_OPCODE_SET,
                            NULL, 0,
                            "foo", 3,
                            NULL,
                            &my_mcd_handler, c,
                            c->cancel_ptr);
    pool_unref(pool);
    pool_commit();

    c->event_loop.Dispatch();

    assert(c->released);
    assert(c->reuse);
    assert(!c->fd.IsDefined());
    assert(c->status == MEMCACHED_STATUS_NO_ERROR);
    assert(!c->value.IsDefined());
    assert(c->value_eof);
    assert(!c->value_abort);
}

static void
test_close_early(struct pool *pool, Context *c)
{
    c->fd = connect_fake_server();
    c->close_value_early = true;

    memcached_client_invoke(pool, c->event_loop,
                            c->fd, FdType::FD_SOCKET, *c,
                            MEMCACHED_OPCODE_SET,
                            NULL, 0,
                            "foo", 3,
                            NULL,
                            &my_mcd_handler, c,
                            c->cancel_ptr);
    pool_unref(pool);
    pool_commit();

    c->event_loop.Dispatch();

    assert(c->released);
    assert(!c->reuse);
    assert(!c->fd.IsDefined());
    assert(c->status == MEMCACHED_STATUS_NO_ERROR);
    assert(!c->value.IsDefined());
    assert(!c->value_eof);
    assert(!c->value_abort);
    assert(c->value_data == 0);
}

static void
test_close_late(struct pool *pool, Context *c)
{
    c->fd = connect_fake_server();
    c->close_value_late = true;

    memcached_client_invoke(pool, c->event_loop,
                            c->fd, FdType::FD_SOCKET, *c,
                            MEMCACHED_OPCODE_SET,
                            NULL, 0,
                            "foo", 3,
                            NULL,
                            &my_mcd_handler, c,
                            c->cancel_ptr);
    pool_unref(pool);
    pool_commit();

    c->event_loop.Dispatch();

    assert(c->released);
    assert(!c->reuse);
    assert(!c->fd.IsDefined());
    assert(c->status == MEMCACHED_STATUS_NO_ERROR);
    assert(!c->value.IsDefined());
    assert(!c->value_eof);
    assert(!c->value_abort);
    assert(c->value_closed);
    assert(c->value_data == 0);
}

static void
test_close_data(struct pool *pool, Context *c)
{
    c->fd = connect_fake_server();
    c->close_value_data = true;

    memcached_client_invoke(pool, c->event_loop,
                            c->fd, FdType::FD_SOCKET, *c,
                            MEMCACHED_OPCODE_SET,
                            NULL, 0,
                            "foo", 3,
                            NULL,
                            &my_mcd_handler, c,
                            c->cancel_ptr);
    pool_unref(pool);
    pool_commit();

    c->event_loop.Dispatch();

    assert(c->released);
    assert(!c->reuse);
    assert(!c->fd.IsDefined());
    assert(c->status == MEMCACHED_STATUS_NO_ERROR);
    assert(!c->value.IsDefined());
    assert(!c->value_eof);
    assert(!c->value_abort);
    assert(c->value_closed);
    assert(c->value_data > 0);
}

static void
test_abort(struct pool *pool, Context *c)
{
    c->fd = connect_fake_server();
    c->close_value_data = true;

    memcached_client_invoke(pool, c->event_loop,
                            c->fd, FdType::FD_SOCKET, *c,
                            MEMCACHED_OPCODE_SET,
                            NULL, 0,
                            "foo", 3,
                            NULL,
                            &my_mcd_handler, c,
                            c->cancel_ptr);
    pool_unref(pool);
    pool_commit();

    c->cancel_ptr.Cancel();

    assert(!c->got_response);
    assert(c->released);
    assert(!c->reuse);
    assert(!c->fd.IsDefined());
    assert(!c->value.IsDefined());
    assert(!c->value_eof);
    assert(!c->value_abort);
}

static void
test_request_value(struct pool *pool, Context *c)
{
    Istream *value;

    c->fd = connect_fake_server();

    value = request_value_new(c->pool, false, false);

    memcached_client_invoke(pool, c->event_loop,
                            c->fd, FdType::FD_SOCKET, *c,
                            MEMCACHED_OPCODE_SET,
                            NULL, 0,
                            "foo", 3,
                            UnusedIstreamPtr(value),
                            &my_mcd_handler, c,
                            request_value_cancel_ptr(*value));
    pool_unref(pool);
    pool_commit();

    c->event_loop.Dispatch();

    assert(c->released);
    assert(c->reuse);
    assert(!c->fd.IsDefined());
    assert(c->status == MEMCACHED_STATUS_NO_ERROR);
    assert(!c->value.IsDefined());
    assert(c->value_eof);
    assert(!c->value_abort);
}

static void
test_request_value_close(struct pool *pool, Context *c)
{
    Istream *value;

    c->fd = connect_fake_server();

    value = request_value_new(c->pool, true, false);

    memcached_client_invoke(pool, c->event_loop,
                            c->fd, FdType::FD_SOCKET, *c,
                            MEMCACHED_OPCODE_SET,
                            NULL, 0,
                            "foo", 3,
                            UnusedIstreamPtr(value),
                            &my_mcd_handler, c,
                            request_value_cancel_ptr(*value));
    pool_unref(pool);
    pool_commit();

    c->event_loop.Dispatch();

    assert(c->released);
    assert(!c->reuse);
    assert(!c->fd.IsDefined());
}

static void
test_request_value_abort(struct pool *pool, Context *c)
{
    Istream *value;

    c->fd = connect_fake_server();

    value = request_value_new(c->pool, false, true);

    memcached_client_invoke(pool, c->event_loop,
                            c->fd, FdType::FD_SOCKET, *c,
                            MEMCACHED_OPCODE_SET,
                            NULL, 0,
                            "foo", 3,
                            UnusedIstreamPtr(value),
                            &my_mcd_handler, c,
                            request_value_cancel_ptr(*value));
    pool_unref(pool);
    pool_commit();

    c->event_loop.Dispatch();

    assert(c->released);
    assert(!c->reuse);
    assert(!c->fd.IsDefined());
}

/*
 * main
 *
 */

static void
run_test(void (*test)(struct pool *pool, Context *c))
{
    Context c;

    c.pool = pool_new_linear(c.root_pool, "test", 16384).release();
    test(c.pool, &c);
    pool_commit();
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    SetupProcess();

    direct_global_init();
    const ScopeFbPoolInit fb_pool_init;

    run_test(test_basic);
    run_test(test_close_early);
    run_test(test_close_late);
    run_test(test_close_data);
    run_test(test_abort);
    run_test(test_request_value);
    run_test(test_request_value_close);
    run_test(test_request_value_abort);
}
