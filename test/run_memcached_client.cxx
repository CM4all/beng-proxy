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
#include "lease.hxx"
#include "istream/istream.hxx"
#include "istream/istream_pipe.hxx"
#include "istream/istream_string.hxx"
#include "istream/sink_fd.hxx"
#include "direct.hxx"
#include "PInstance.hxx"
#include "event/ShutdownListener.hxx"
#include "fb_pool.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "net/RConnectSocket.hxx"
#include "system/SetupProcess.hxx"
#include "io/FileDescriptor.hxx"
#include "util/ByteOrder.hxx"
#include "util/Cancellable.hxx"
#include "util/PrintException.hxx"

#include <unistd.h>
#include <stdio.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>

struct Context final : PInstance, Lease {
    struct pool *pool;

    ShutdownListener shutdown_listener;
    CancellablePointer cancel_ptr;

    UniqueSocketDescriptor s;
    bool idle = false, reuse, aborted = false;
    enum memcached_response_status status;

    SinkFd *value;
    bool value_eof = false, value_abort = false, value_closed = false;

    Context()
        :shutdown_listener(event_loop, BIND_THIS_METHOD(ShutdownCallback)) {}

    void ShutdownCallback();

    /* virtual methods from class Lease */
    void ReleaseLease(bool _reuse) noexcept override {
        assert(!idle);
        assert(s.IsDefined());

        idle = true;
        reuse = _reuse;

        s.Close();
    }
};

void
Context::ShutdownCallback()
{
    if (value != nullptr) {
        sink_fd_close(value);
        value = nullptr;
        value_abort = true;
    } else {
        aborted = true;
        cancel_ptr.Cancel();
    }
}

/*
 * sink_fd handler
 *
 */

static void
my_sink_fd_input_eof(void *ctx)
{
    auto *c = (Context *)ctx;

    c->value = NULL;
    c->value_eof = true;

    c->shutdown_listener.Disable();
}

static void
my_sink_fd_input_error(std::exception_ptr ep, void *ctx)
{
    auto *c = (Context *)ctx;

    PrintException(ep);

    c->value = NULL;
    c->value_abort = true;

    c->shutdown_listener.Disable();
}

static bool
my_sink_fd_send_error(int error, void *ctx)
{
    auto *c = (Context *)ctx;

    fprintf(stderr, "%s\n", strerror(error));

    c->value = NULL;
    c->value_abort = true;

    c->shutdown_listener.Disable();

    return true;
}

static constexpr SinkFdHandler my_sink_fd_handler = {
    .input_eof = my_sink_fd_input_eof,
    .input_error = my_sink_fd_input_error,
    .send_error = my_sink_fd_send_error,
};

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
                Istream *value, void *ctx)
{
    auto *c = (Context *)ctx;

    fprintf(stderr, "status=%d\n", status);

    c->status = status;

    if (value != NULL) {
        value = istream_pipe_new(c->pool, *value, nullptr);
        c->value = sink_fd_new(c->event_loop, *c->pool, *value,
                               FileDescriptor(STDOUT_FILENO),
                               guess_fd_type(STDOUT_FILENO),
                               my_sink_fd_handler, c);
        value->Read();
    } else {
        c->value_eof = true;
        c->shutdown_listener.Disable();
    }
}

static void
my_mcd_error(std::exception_ptr ep, void *ctx)
{
    auto *c = (Context *)ctx;

    PrintException(ep);

    c->status = (memcached_response_status)-1;
    c->value_eof = true;

    c->shutdown_listener.Disable();
}

static const struct memcached_client_handler my_mcd_handler = {
    .response = my_mcd_response,
    .error = my_mcd_error,
};

/*
 * main
 *
 */

int main(int argc, char **argv) {
    struct pool *pool;
    enum memcached_opcode opcode;
    const char *key, *value;
    const void *extras;
    size_t extras_length;
    struct memcached_set_extras set_extras;

    if (argc < 3 || argc > 5) {
        fprintf(stderr, "usage: run-memcached-client HOST[:PORT] OPCODE [KEY] [VALUE]\n");
        return 1;
    }

    if (strcmp(argv[2], "get") == 0)
        opcode = MEMCACHED_OPCODE_GET;
    else if (strcmp(argv[2], "set") == 0)
        opcode = MEMCACHED_OPCODE_SET;
    else if (strcmp(argv[2], "delete") == 0)
        opcode = MEMCACHED_OPCODE_DELETE;
    else {
        fprintf(stderr, "unknown opcode\n");
        return 1;
    }

    key = argc > 3 ? argv[3] : NULL;
    value = argc > 4 ? argv[4] : NULL;

    if (opcode == MEMCACHED_OPCODE_SET) {
        set_extras.flags = 0;
        set_extras.expiration = ToBE32(300);
        extras = &set_extras;
        extras_length = sizeof(set_extras);
    } else {
        extras = NULL;
        extras_length = 0;
    }

    direct_global_init();

    /* connect socket */

    Context ctx;
    ctx.s = ResolveConnectStreamSocket(argv[1], 11211);
    ctx.s.SetNoDelay();

    /* initialize */

    SetupProcess();
    const ScopeFbPoolInit fb_pool_init;

    ctx.shutdown_listener.Enable();

    ctx.pool = pool = pool_new_linear(ctx.root_pool, "test", 8192);

    /* run test */

    memcached_client_invoke(pool, ctx.event_loop, ctx.s, FdType::FD_TCP,
                            ctx,
                            opcode,
                            extras, extras_length,
                            key, key != NULL ? strlen(key) : 0,
                            value != NULL ? istream_string_new(pool, value) : NULL,
                            &my_mcd_handler, &ctx,
                            ctx.cancel_ptr);

    ctx.event_loop.Dispatch();

    assert(ctx.value_eof || ctx.value_abort || ctx.aborted);

    /* cleanup */

    pool_unref(pool);
    pool_commit();

    return ctx.value_eof ? 0 : 2;
}
