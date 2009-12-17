#include "memcached-client.h"
#include "lease.h"
#include "async.h"
#include "socket-util.h"

#include <socket/resolver.h>
#include <socket/util.h>

#include <glib.h>
#include <event.h>

#include <unistd.h>
#include <stdio.h>
#include <sys/signal.h>
#include <netdb.h>
#include <errno.h>

struct context {
    int fd;
    bool idle, reuse, aborted;
    enum memcached_response_status status;

    istream_t value;
    bool value_eof, value_abort;
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
 * istream handler
 *
 */

static size_t
my_istream_data(const void *data, size_t length, void *ctx)
{
    struct context *c = ctx;
    ssize_t nbytes;

    nbytes = write(1, data, length);
    if (nbytes <= 0) {
        istream_close(c->value);
        return 0;
    }

    return (size_t)nbytes;
}

static void
my_istream_eof(void *ctx)
{
    struct context *c = ctx;

    c->value = NULL;
    c->value_eof = true;
}

static void
my_istream_abort(void *ctx)
{
    struct context *c = ctx;

    c->value = NULL;
    c->value_abort = true;
}

static const struct istream_handler my_istream_handler = {
    .data = my_istream_data,
    .eof = my_istream_eof,
    .abort = my_istream_abort,
};

/*
 * memcached_response_handler_t
 *
 */

static void
my_response_handler(enum memcached_response_status status,
                    G_GNUC_UNUSED const void *extras,
                    G_GNUC_UNUSED size_t extras_length,
                    G_GNUC_UNUSED const void *key,
                    G_GNUC_UNUSED size_t key_length,
                    istream_t value, void *ctx)
{
    struct context *c = ctx;

    fprintf(stderr, "status=%d\n", status);

    c->status = status;

    if (value != NULL)
        istream_assign_handler(&c->value, value, &my_istream_handler, c, 0);
    else
        c->value_eof = true;
}

/*
 * main
 *
 */

int main(int argc, char **argv) {
    int ret;
    struct addrinfo hints, *ai;
    struct event_base *event_base;
    pool_t root_pool, pool;
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

    socket_set_nonblock(ctx.fd, true);
    socket_set_nodelay(ctx.fd, true);

    /* initialize */

    signal(SIGPIPE, SIG_IGN);

    event_base = event_init();

    root_pool = pool_new_libc(NULL, "root");
    pool = pool_new_linear(root_pool, "test", 8192);

    /* run test */

    memcached_client_invoke(pool, ctx.fd, &memcached_socket_lease, &ctx,
                            opcode,
                            extras, extras_length,
                            key, key != NULL ? strlen(key) : 0,
                            value != NULL ? istream_string_new(pool, value) : NULL,
                            &my_response_handler, &ctx,
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

    return ctx.value_eof ? 0 : 2;
}
