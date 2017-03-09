#include "memcached/memcached_client.hxx"
#include "lease.hxx"
#include "system/fd-util.h"
#include "istream/istream.hxx"
#include "istream/istream_pipe.hxx"
#include "istream/istream_string.hxx"
#include "istream/sink_fd.hxx"
#include "direct.hxx"
#include "event/Loop.hxx"
#include "event/ShutdownListener.hxx"
#include "fb_pool.hxx"
#include "RootPool.hxx"
#include "net/SocketDescriptor.hxx"
#include "net/RConnectSocket.hxx"
#include "system/SetupProcess.hxx"
#include "io/FileDescriptor.hxx"
#include "util/ByteOrder.hxx"
#include "util/Cancellable.hxx"

#include <socket/util.h>

#include <glib.h>

#include <unistd.h>
#include <stdio.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>

struct Context final : Lease {
    EventLoop event_loop;

    struct pool *pool;

    ShutdownListener shutdown_listener;
    CancellablePointer cancel_ptr;

    SocketDescriptor s;
    bool idle = false, reuse, aborted = false;
    enum memcached_response_status status;

    SinkFd *value;
    bool value_eof = false, value_abort = false, value_closed = false;

    Context()
        :shutdown_listener(event_loop, BIND_THIS_METHOD(ShutdownCallback)) {}

    void ShutdownCallback();

    /* virtual methods from class Lease */
    void ReleaseLease(bool _reuse) override {
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
my_sink_fd_input_error(GError *error, void *ctx)
{
    auto *c = (Context *)ctx;

    g_printerr("%s\n", error->message);
    g_error_free(error);

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
my_mcd_error(GError *error, void *ctx)
{
    auto *c = (Context *)ctx;

    g_error_free(error);

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

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_ADDRCONFIG|AI_PASSIVE;
    hints.ai_socktype = SOCK_STREAM;

    Context ctx;
    ctx.s = ResolveConnectSocket(argv[1], 11211, hints);

    socket_set_nodelay(ctx.s.Get(), true);

    /* initialize */

    SetupProcess();
    const ScopeFbPoolInit fb_pool_init;

    ctx.shutdown_listener.Enable();

    RootPool root_pool;
    ctx.pool = pool = pool_new_linear(root_pool, "test", 8192);

    /* run test */

    memcached_client_invoke(pool, ctx.event_loop, ctx.s.Get(), FdType::FD_TCP,
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
