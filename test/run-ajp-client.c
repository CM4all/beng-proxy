#include "ajp-client.h"
#include "http-response.h"
#include "async.h"
#include "fd_util.h"
#include "lease.h"
#include "direct.h"
#include "istream-file.h"
#include "istream.h"
#include "shutdown_listener.h"
#include "client-socket.h"

#include <inline/compiler.h>
#include <socket/resolver.h>
#include <socket/util.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <event.h>
#include <netdb.h>
#include <errno.h>

struct context {
    struct pool *pool;

    struct shutdown_listener shutdown_listener;

    struct async_operation_ref async_ref;

    http_method_t method;
    const char *uri;
    struct istream *request_body;

    int fd;
    bool idle, reuse, aborted;
    http_status_t status;

    struct istream *body;
    bool body_eof, body_abort, body_closed;
};


static void
shutdown_callback(void *ctx)
{
    struct context *c = ctx;

    if (c->body != NULL) {
        istream_free_handler(&c->body);
        c->body_abort = true;
    } else {
        c->aborted = true;
        async_abort(&c->async_ref);
    }
}

/*
 * socket lease
 *
 */

static void
ajp_socket_release(bool reuse, void *ctx)
{
    struct context *c = ctx;

    assert(!c->idle);
    assert(c->fd >= 0);

    c->idle = true;
    c->reuse = reuse;

    close(c->fd);
    c->fd = -1;
}

static const struct lease ajp_socket_lease = {
    .release = ajp_socket_release,
};


/*
 * istream handler
 *
 */

static size_t
my_istream_data(const void *data, size_t length, void *ctx)
{
    struct context *c = ctx;

    ssize_t nbytes = write(1, data, length);
    if (nbytes <= 0) {
        c->body_closed = true;
        istream_free_handler(&c->body);
        return 0;
    }

    return (size_t)nbytes;
}

static void
my_istream_eof(void *ctx)
{
    struct context *c = ctx;

    c->body = NULL;
    c->body_eof = true;

    shutdown_listener_deinit(&c->shutdown_listener);
}

static void
my_istream_abort(GError *error, void *ctx)
{
    struct context *c = ctx;

    g_error_free(error);

    c->body = NULL;
    c->body_abort = true;

    shutdown_listener_deinit(&c->shutdown_listener);
}

static const struct istream_handler my_istream_handler = {
    .data = my_istream_data,
    .eof = my_istream_eof,
    .abort = my_istream_abort,
};


/*
 * http_response_handler
 *
 */

static void
my_response(http_status_t status, struct strmap *headers gcc_unused,
            struct istream *body gcc_unused,
            void *ctx)
{
    struct context *c = ctx;

    c->status = status;

    if (body != NULL)
        istream_assign_handler(&c->body, body, &my_istream_handler, c, 0);
    else
        c->body_eof = true;
}

static void
my_response_abort(GError *error, void *ctx)
{
    struct context *c = ctx;

    g_printerr("%s\n", error->message);
    g_error_free(error);

    c->aborted = true;

    shutdown_listener_deinit(&c->shutdown_listener);
}

static const struct http_response_handler my_response_handler = {
    .response = my_response,
    .abort = my_response_abort,
};


/*
 * client_socket_handler
 *
 */

static void
my_client_socket_success(int fd, void *ctx)
{
    struct context *c = ctx;

    c->fd = fd;

    ajp_client_request(c->pool, fd, ISTREAM_TCP, &ajp_socket_lease, c,
                       "http", "127.0.0.1", "localhost",
                       "localhost", 80, false,
                       c->method, c->uri, NULL, c->request_body,
                       &my_response_handler, c,
                       &c->async_ref);
}

static void
my_client_socket_timeout(void *ctx)
{
    struct context *c = ctx;

    g_printerr("Connect timeout\n");

    c->aborted = true;

    if (c->request_body != NULL)
        istream_close_unused(c->request_body);

    shutdown_listener_deinit(&c->shutdown_listener);
}

static void
my_client_socket_error(GError *error, void *ctx)
{
    struct context *c = ctx;

    g_printerr("%s\n", error->message);
    g_error_free(error);

    c->aborted = true;

    if (c->request_body != NULL)
        istream_close_unused(c->request_body);

    shutdown_listener_deinit(&c->shutdown_listener);
}

static const struct client_socket_handler my_client_socket_handler = {
    .success = my_client_socket_success,
    .timeout = my_client_socket_timeout,
    .error = my_client_socket_error,
};

/*
 * main
 *
 */

int
main(int argc, char **argv)
{
    static struct context ctx;

    if (argc < 3 || argc > 4) {
        fprintf(stderr, "usage: run-ajp-client HOST[:PORT] URI [BODY]\n");
        return 1;
    }

    direct_global_init();

    /* connect socket */

    struct addrinfo hints, *ai;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;

    int ret = socket_resolve_host_port(argv[1], 8009, &hints, &ai);
    if (ret != 0) {
        fprintf(stderr, "Failed to resolve host name\n");
        return 2;
    }

    /* initialize */

    signal(SIGPIPE, SIG_IGN);

    struct event_base *event_base = event_init();

    shutdown_listener_init(&ctx.shutdown_listener, shutdown_callback, &ctx);

    struct pool *root_pool = pool_new_libc(NULL, "root");
    struct pool *pool = pool_new_linear(root_pool, "test", 8192);
    ctx.pool = pool;

    /* open request body */

    if (argc >= 4) {
        struct stat st;

        ret = stat(argv[3], &st);
        if (ret < 0) {
            fprintf(stderr, "Failed to stat %s: %s\n",
                    argv[3], strerror(errno));
            return 2;
        }

        ctx.method = HTTP_METHOD_POST;
        ctx.request_body = istream_file_new(pool, argv[3], st.st_size);
    } else {
        ctx.method = HTTP_METHOD_GET;
        ctx.request_body = NULL;
    }

    ctx.uri = argv[2];

    /* connect */

    client_socket_new(pool,
                      ai->ai_family, ai->ai_socktype, ai->ai_protocol,
                      ai->ai_addr, ai->ai_addrlen,
                      30,
                      &my_client_socket_handler, &ctx,
                      &ctx.async_ref);
    freeaddrinfo(ai);

    /* run test */

    event_dispatch();

    assert(ctx.body_eof || ctx.body_abort || ctx.aborted);

    fprintf(stderr, "reuse=%d\n", ctx.reuse);

    /* cleanup */

    pool_unref(pool);
    pool_commit();

    pool_unref(root_pool);
    pool_commit();
    pool_recycler_clear();

    event_base_free(event_base);
    direct_global_deinit();

    return ctx.body_eof ? 0 : 2;
}
