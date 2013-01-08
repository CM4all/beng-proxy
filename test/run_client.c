#include "ajp-client.h"
#include "strmap.h"
#include "header-writer.h"
#include "http-client.h"
#include "http-response.h"
#include "async.h"
#include "fd_util.h"
#include "lease.h"
#include "direct.h"
#include "istream-file.h"
#include "istream.h"
#include "sink_fd.h"
#include "direct.h"
#include "shutdown_listener.h"
#include "client-socket.h"
#include "tpool.h"

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

struct parsed_url {
    enum {
        HTTP, AJP,
    } protocol;

    char *host;

    int default_port;

    const char *uri;
};

static bool
parse_url(struct parsed_url *dest, const char *url)
{
    assert(dest != NULL);
    assert(url != NULL);

    if (memcmp(url, "ajp://", 6) == 0) {
        url += 6;
        dest->protocol = AJP;
        dest->default_port = 8009;
    } else if (memcmp(url, "http://", 7) == 0) {
        url += 7;
        dest->protocol = HTTP;
        dest->default_port = 80;
    } else
        return false;

    dest->uri = strchr(url, '/');
    if (dest->uri == NULL || dest->uri == url)
        return false;

    dest->host = g_strndup(url, dest->uri - url);
    return true;
}

struct context {
    struct pool *pool;

    struct parsed_url url;

    struct shutdown_listener shutdown_listener;

    struct async_operation_ref async_ref;

    http_method_t method;
    struct istream *request_body;

    int fd;
    bool idle, reuse, aborted;
    http_status_t status;

    struct sink_fd *body;
    bool body_eof, body_abort, body_closed;
};


static void
shutdown_callback(void *ctx)
{
    struct context *c = ctx;

    if (c->body != NULL) {
        sink_fd_close(c->body);
        c->body = NULL;
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

static void
my_sink_fd_input_eof(void *ctx)
{
    struct context *c = ctx;

    c->body = NULL;
    c->body_eof = true;

    shutdown_listener_deinit(&c->shutdown_listener);
}

static void
my_sink_fd_input_error(GError *error, void *ctx)
{
    struct context *c = ctx;

    g_printerr("%s\n", error->message);
    g_error_free(error);

    c->body = NULL;
    c->body_abort = true;

    shutdown_listener_deinit(&c->shutdown_listener);
}

static bool
my_sink_fd_send_error(int error, void *ctx)
{
    struct context *c = ctx;

    g_printerr("%s\n", g_strerror(error));

    c->body = NULL;
    c->body_abort = true;

    shutdown_listener_deinit(&c->shutdown_listener);
    return true;
}

static const struct sink_fd_handler my_sink_fd_handler = {
    .input_eof = my_sink_fd_input_eof,
    .input_error = my_sink_fd_input_error,
    .send_error = my_sink_fd_send_error,
};


/*
 * http_response_handler
 *
 */

static void
my_response(http_status_t status, struct strmap *headers gcc_unused,
            struct istream *body,
            void *ctx)
{
    struct context *c = ctx;

    c->status = status;

    if (body != NULL) {
        body = istream_pipe_new(c->pool, body, NULL);
        c->body = sink_fd_new(c->pool, body, 1, guess_fd_type(1),
                              &my_sink_fd_handler, c);
        istream_read(body);
    } else
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

    struct strmap *headers = strmap_new(c->pool, 17);
    strmap_add(headers, "host", c->url.host);

    switch (c->url.protocol) {
    case AJP:
        ajp_client_request(c->pool, fd, ISTREAM_TCP, &ajp_socket_lease, c,
                           "http", "127.0.0.1", "localhost",
                           "localhost", 80, false,
                           c->method, c->url.uri, headers, c->request_body,
                           &my_response_handler, c,
                           &c->async_ref);
        break;

    case HTTP:
        http_client_request(c->pool, fd, ISTREAM_TCP, &ajp_socket_lease, c,
                            c->method, c->url.uri,
                            headers_dup(c->pool, headers),
                            c->request_body, false,
                            &my_response_handler, c,
                            &c->async_ref);
        break;
    }
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

    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: run-ajp-client URL [BODY]\n");
        return EXIT_FAILURE;
    }

    if (!parse_url(&ctx.url, argv[1])) {
        fprintf(stderr, "Invalid or unsupported URL.\n");
        return EXIT_FAILURE;
    }

    direct_global_init();

    /* connect socket */

    struct addrinfo hints, *ai;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;

    int ret = socket_resolve_host_port(ctx.url.host, ctx.url.default_port,
                                       &hints, &ai);
    if (ret != 0) {
        fprintf(stderr, "Failed to resolve host name\n");
        return EXIT_FAILURE;
    }

    /* initialize */

    signal(SIGPIPE, SIG_IGN);

    struct event_base *event_base = event_init();

    shutdown_listener_init(&ctx.shutdown_listener, shutdown_callback, &ctx);

    struct pool *root_pool = pool_new_libc(NULL, "root");
    tpool_init(root_pool);

    struct pool *pool = pool_new_linear(root_pool, "test", 8192);
    ctx.pool = pool;

    /* open request body */

    if (argc >= 3) {
        struct stat st;

        ret = stat(argv[2], &st);
        if (ret < 0) {
            fprintf(stderr, "Failed to stat %s: %s\n",
                    argv[2], strerror(errno));
            return EXIT_FAILURE;
        }

        ctx.method = HTTP_METHOD_POST;
        ctx.request_body = istream_file_new(pool, argv[2], st.st_size);
    } else {
        ctx.method = HTTP_METHOD_GET;
        ctx.request_body = NULL;
    }

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

    tpool_deinit();
    pool_unref(root_pool);
    pool_commit();
    pool_recycler_clear();

    event_base_free(event_base);
    direct_global_deinit();

    g_free(ctx.url.host);

    return ctx.body_eof ? EXIT_SUCCESS : EXIT_FAILURE;
}
