#include "memcached/memcached_client.hxx"
#include "lease.hxx"
#include "async.hxx"
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
#include "system/SetupProcess.hxx"
#include "util/ByteOrder.hxx"

#include <socket/resolver.h>
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
    struct async_operation_ref async_ref;

    int fd;
    bool idle = false, reuse, aborted = false;
    enum memcached_response_status status;

    SinkFd *value;
    bool value_eof = false, value_abort = false, value_closed = false;

    Context()
        :shutdown_listener(event_loop, ShutdownCallback, this) {}

    static void ShutdownCallback(void *ctx);

    /* virtual methods from class Lease */
    void ReleaseLease(bool _reuse) override {
        assert(!idle);
        assert(fd >= 0);

        idle = true;
        reuse = _reuse;

        close(fd);
        fd = -1;
    }
};

void
Context::ShutdownCallback(void *ctx)
{
    auto *c = (Context *)ctx;

    if (c->value != NULL) {
        sink_fd_close(c->value);
        c->value = NULL;
        c->value_abort = true;
    } else {
        c->aborted = true;
        c->async_ref.Abort();
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

    g_printerr("%s\n", g_strerror(error));

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
        c->value = sink_fd_new(*c->pool, *value,
                               1, guess_fd_type(1),
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
    int ret;
    struct addrinfo hints, *ai;
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

    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;

    ret = socket_resolve_host_port(argv[1], 11211, &hints, &ai);
    if (ret != 0) {
        fprintf(stderr, "Failed to resolve host name\n");
        return 2;
    }

    Context ctx;
    ctx.fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (ctx.fd < 0) {
        fprintf(stderr, "socket() failed: %s\n", strerror(errno));
        return 2;
    }

    ret = connect(ctx.fd, ai->ai_addr, ai->ai_addrlen);
    if (ret < 0) {
        fprintf(stderr, "connect() failed: %s\n", strerror(errno));
        return 2;
    }

    freeaddrinfo(ai);

    fd_set_nonblock(ctx.fd, true);
    socket_set_nodelay(ctx.fd, true);

    /* initialize */

    SetupProcess();

    fb_pool_init(ctx.event_loop, false);
    ctx.shutdown_listener.Enable();

    RootPool root_pool;
    ctx.pool = pool = pool_new_linear(root_pool, "test", 8192);

    /* run test */

    memcached_client_invoke(pool, ctx.event_loop, ctx.fd, FdType::FD_TCP,
                            ctx,
                            opcode,
                            extras, extras_length,
                            key, key != NULL ? strlen(key) : 0,
                            value != NULL ? istream_string_new(pool, value) : NULL,
                            &my_mcd_handler, &ctx,
                            &ctx.async_ref);

    ctx.event_loop.Dispatch();

    assert(ctx.value_eof || ctx.value_abort || ctx.aborted);

    /* cleanup */

    pool_unref(pool);
    pool_commit();

    fb_pool_deinit();

    return ctx.value_eof ? 0 : 2;
}
