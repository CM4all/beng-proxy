#include "memcached-client.h"
#include "lease.h"
#include "async.h"
#include "fd-util.h"
#include "istream.h"
#include "sink_fd.h"
#include "direct.h"

#include <socket/resolver.h>
#include <socket/util.h>

#include <glib.h>
#include <event.h>

#include <unistd.h>
#include <stdio.h>
#include <sys/signal.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>

struct context {
    struct pool *pool;

    int fd;
    bool idle, reuse, aborted;
    enum memcached_response_status status;

    struct sink_fd *value;
    bool value_eof, value_abort, value_closed;
};


/*
 * socket lease
 *
 */

static void
memcached_socket_release(bool reuse, void *ctx)
{
    struct context *c = ctx;

    assert(!c->idle);
    assert(c->fd >= 0);

    c->idle = true;
    c->reuse = reuse;

    close(c->fd);
    c->fd = -1;
}

static const struct lease memcached_socket_lease = {
    .release = memcached_socket_release,
};


/*
 * sink_fd handler
 *
 */

static void
my_sink_fd_input_eof(void *ctx)
{
    struct context *c = ctx;

    c->value = NULL;
    c->value_eof = true;
}

static void
my_sink_fd_input_error(GError *error, void *ctx)
{
    struct context *c = ctx;

    g_printerr("%s\n", error->message);
    g_error_free(error);

    c->value = NULL;
    c->value_abort = true;
}

static bool
my_sink_fd_send_error(int error, void *ctx)
{
    struct context *c = ctx;

    g_printerr("%s\n", g_strerror(error));

    c->value = NULL;
    c->value_abort = true;

    return true;
}

static const struct sink_fd_handler my_sink_fd_handler = {
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
                G_GNUC_UNUSED const void *extras,
                G_GNUC_UNUSED size_t extras_length,
                G_GNUC_UNUSED const void *key,
                G_GNUC_UNUSED size_t key_length,
                struct istream *value, void *ctx)
{
    struct context *c = ctx;

    fprintf(stderr, "status=%d\n", status);

    c->status = status;

    if (value != NULL) {
        value = istream_pipe_new(c->pool, value, NULL);
        c->value = sink_fd_new(c->pool, value,
                               1, guess_fd_type(1),
                               &my_sink_fd_handler, c);
        istream_read(value);
    } else
        c->value_eof = true;
}

static void
my_mcd_error(GError *error, void *ctx)
{
    struct context *c = ctx;

    g_error_free(error);

    c->status = -1;
    c->value_eof = true;
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
    struct event_base *event_base;
    struct pool *root_pool, *pool;
    enum memcached_opcode opcode;
    const char *key, *value;
    const void *extras;
    size_t extras_length;
    struct memcached_set_extras set_extras;
    static struct context ctx;
    struct async_operation_ref async_ref;

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
        set_extras.expiration = htonl(300);
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

    signal(SIGPIPE, SIG_IGN);

    event_base = event_init();

    root_pool = pool_new_libc(NULL, "root");
    ctx.pool = pool = pool_new_linear(root_pool, "test", 8192);

    /* run test */

    memcached_client_invoke(pool, ctx.fd, ISTREAM_TCP,
                            &memcached_socket_lease, &ctx,
                            opcode,
                            extras, extras_length,
                            key, key != NULL ? strlen(key) : 0,
                            value != NULL ? istream_string_new(pool, value) : NULL,
                            &my_mcd_handler, &ctx,
                            &async_ref);

    event_dispatch();

    assert(ctx.value_eof || ctx.value_abort || ctx.aborted);

    /* cleanup */

    pool_unref(pool);
    pool_commit();

    pool_unref(root_pool);
    pool_commit();
    pool_recycler_clear();

    event_base_free(event_base);
    direct_global_deinit();

    return ctx.value_eof ? 0 : 2;
}
