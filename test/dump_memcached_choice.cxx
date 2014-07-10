#include "memcached_client.hxx"
#include "http_cache_internal.hxx"
#include "lease.h"
#include "async.h"
#include "fd-util.h"
#include "strmap.hxx"
#include "tpool.h"
#include "serialize.hxx"
#include "sink_buffer.hxx"
#include "istream.h"
#include "direct.h"
#include "fb_pool.h"
#include "util/ConstBuffer.hxx"

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
    bool idle, reuse;

    struct istream *value;
    bool value_eof, value_abort;

    struct async_operation_ref async_ref;
};

static void
dump_choice(const struct http_cache_document *document)
{
    printf("expires=%ld\n", (long)(document->info.expires - time(NULL)));

    for (const auto &i : *document->vary)
        printf("\t%s: %s\n", i.key, i.value);
}


/*
 * socket lease
 *
 */

static void
memcached_socket_release(bool reuse, void *ctx)
{
    context *c = (context *)ctx;

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
my_sink_done(void *data0, size_t length, gcc_unused void *ctx)
{
    struct http_cache_document document;
    /*uint32_t magic;*/

    ConstBuffer<void> data(data0, length);

    while (!data.IsEmpty()) {
        const AutoRewindPool auto_rewind(tpool);

        /*magic = */deserialize_uint32(data);
        /*
        if (magic != CHOICE_MAGIC)
            break;
        */

        document.info.expires = deserialize_uint64(data);
        document.vary = deserialize_strmap(data, tpool);

        if (data.IsNull())
            /* deserialization failure */
            break;

        dump_choice(&document);
    }
}

static void
my_sink_error(GError *error, gcc_unused void *ctx)
{
    fprintf(stderr, "sink_buffer has failed: %s\n", error->message);
    g_error_free(error);
}

static const struct sink_buffer_handler my_sink_handler = {
    .done = my_sink_done,
    .error = my_sink_error,
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
                struct istream *value, void *ctx)
{
    context *c = (context *)ctx;

    if (status != MEMCACHED_STATUS_NO_ERROR || value == NULL) {
        fprintf(stderr, "status=%d\n", status);
        if (value != NULL)
            istream_close_unused(value);
        return;
    }

    sink_buffer_new(c->pool, value,
                    &my_sink_handler, c,
                    &c->async_ref);
}

static void
my_mcd_error(GError *error, gcc_unused void *ctx)
{
    fprintf(stderr, "%s\n", error->message);
    g_error_free(error);
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
    int fd, ret;
    struct addrinfo hints, *ai;
    struct event_base *event_base;
    struct pool *root_pool;
    const char *key;
    static struct context ctx;

    if (argc != 3) {
        fprintf(stderr, "usage: run-memcached-client HOST[:PORT] URI\n");
        return 1;
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

    fd_set_nonblock(fd, true);
    socket_set_nodelay(fd, true);

    /* initialize */

    signal(SIGPIPE, SIG_IGN);

    event_base = event_init();
    fb_pool_init(false);

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
                            &my_mcd_handler, &ctx,
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

    fb_pool_deinit();
    event_base_free(event_base);
    direct_global_deinit();

    return ctx.value_eof ? 0 : 2;
}
