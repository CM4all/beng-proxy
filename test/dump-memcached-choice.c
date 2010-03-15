#include "memcached-client.h"
#include "http-cache-internal.h"
#include "lease.h"
#include "async.h"
#include "socket-util.h"
#include "strref.h"
#include "strmap.h"
#include "tpool.h"
#include "serialize.h"
#include "sink-impl.h"

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
    pool_t pool;

    int fd;
    bool idle, reuse;

    istream_t value;
    bool value_eof, value_abort;

    struct async_operation_ref async_ref;
};

static void
dump_choice(const struct http_cache_document *document)
{
    const struct strmap_pair *pair;

    printf("expires=%ld\n", (long)(document->info.expires - time(NULL)));

    strmap_rewind(document->vary);
    while ((pair = strmap_next(document->vary)) != NULL)
        printf("\t%s: %s\n", pair->key, pair->value);
}


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
 * sink_buffer callback
 *
 */

static void
my_sink_callback(void *data0, size_t length, G_GNUC_UNUSED void *ctx)
{
    struct strref data;
    struct pool_mark mark;
    struct http_cache_document document;
    uint32_t magic;

    if (data0 == NULL) {
        fprintf(stderr, "sink_buffer has failed\n");
        return;
    }

    strref_set(&data, data0, length);

    while (!strref_is_empty(&data)) {

        pool_mark(tpool, &mark);

        magic = deserialize_uint32(&data);
        /*
        if (magic != CHOICE_MAGIC)
            break;
        */

        document.info.expires = deserialize_uint64(&data);
        document.vary = deserialize_strmap(&data, tpool);

        if (strref_is_null(&data)) {
            /* deserialization failure */
            pool_rewind(tpool, &mark);
            break;
        }

        dump_choice(&document);

        pool_rewind(tpool, &mark);
    }
}


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

    if (status != MEMCACHED_STATUS_NO_ERROR || value == NULL) {
        fprintf(stderr, "status=%d\n", status);
        if (value != NULL)
            istream_close(value);
        return;
    }

    sink_buffer_new(c->pool, value,
                    my_sink_callback, c,
                    &c->async_ref);
}

/*
 * main
 *
 */

int main(int argc, char **argv) {
    int fd, ret;
    struct addrinfo hints, *ai;
    struct event_base *event_base;
    pool_t root_pool;
    const char *key;
    static struct context ctx;

    if (argc != 3) {
        fprintf(stderr, "usage: run-memcached-client HOST[:PORT] URI\n");
        return 1;
    }

    /* connect socket */

    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;

    ret = socket_resolve_host_port(argv[1], 11211, &hints, &ai);
    if (ret != 0) {
        fprintf(stderr, "Failed to resolve host name\n");
        return 2;
    }

    fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0) {
        fprintf(stderr, "socket() failed: %s\n", strerror(errno));
        return 2;
    }

    ret = connect(fd, ai->ai_addr, ai->ai_addrlen);
    if (ret < 0) {
        fprintf(stderr, "connect() failed: %s\n", strerror(errno));
        return 2;
    }

    freeaddrinfo(ai);

    socket_set_nonblock(fd, true);
    socket_set_nodelay(fd, true);

    /* initialize */

    signal(SIGPIPE, SIG_IGN);

    event_base = event_init();

    root_pool = pool_new_libc(NULL, "root");
    tpool_init(root_pool);
    ctx.pool = pool_new_linear(root_pool, "test", 8192);

    key = p_strcat(ctx.pool, argv[2], " choice", NULL);
    printf("key='%s'\n", key);

    /* send memcached request */

    memcached_client_invoke(ctx.pool, fd, ISTREAM_TCP,
                            &memcached_socket_lease, &ctx,
                            MEMCACHED_OPCODE_GET,
                            NULL, 0,
                            key, strlen(key),
                            NULL,
                            &my_response_handler, &ctx,
                            &ctx.async_ref);
    pool_unref(ctx.pool);
    pool_commit();

    event_dispatch();

    /* cleanup */

    tpool_deinit();
    pool_commit();

    pool_unref(root_pool);
    pool_commit();
    pool_recycler_clear();

    event_base_free(event_base);

    return ctx.value_eof ? 0 : 2;
}
