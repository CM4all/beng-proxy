#include "http-server.h"
#include "duplex.h"
#include "direct.h"
#include "sink-impl.h"
#include "istream.h"
#include "pool.h"
#include "async.h"
#include "shutdown_listener.h"
#include "fb_pool.h"

#include <event.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct context {
    struct async_operation operation;

    struct shutdown_listener shutdown_listener;

    enum {
        MODE_NULL,
        MODE_MIRROR,
        MODE_DUMMY,
        MODE_FIXED,
        MODE_HOLD,
    } mode;

    struct http_server_connection *connection;

    struct istream *request_body;

    struct event timer;
};

static void
shutdown_callback(void *ctx)
{
    struct context *c = ctx;

    http_server_connection_close(c->connection);
}

static void
timer_callback(G_GNUC_UNUSED int fd, G_GNUC_UNUSED short event,
                           void *_ctx)
{
    struct context *ctx = _ctx;

    http_server_connection_close(ctx->connection);
    shutdown_listener_deinit(&ctx->shutdown_listener);
}

/*
 * async operation
 *
 */

static void
my_abort(struct async_operation *ao)
{
    struct context *ctx = (struct context *)ao;

    if (ctx->request_body != NULL)
        istream_close_unused(ctx->request_body);

    evtimer_del(&ctx->timer);
}

static const struct async_operation_class my_operation = {
    .abort = my_abort,
};

/*
 * http_server handler
 *
 */

static void
my_request(struct http_server_request *request, void *_ctx,
           struct async_operation_ref *async_ref gcc_unused)
{
    struct context *ctx = _ctx;

    switch (ctx->mode) {
        struct istream *body;
        static const char data[0x100];

    case MODE_NULL:
        if (request->body != NULL)
            sink_null_new(request->body);

        http_server_response(request, HTTP_STATUS_NO_CONTENT, NULL, NULL);
        break;

    case MODE_MIRROR:
        http_server_response(request,
                             request->body == NULL
                             ? HTTP_STATUS_NO_CONTENT : HTTP_STATUS_OK,
                             NULL,
                             request->body);
        break;

    case MODE_DUMMY:
        if (request->body != NULL)
            sink_null_new(request->body);

        body = istream_head_new(request->pool,
                                istream_zero_new(request->pool),
                                256, false);
        body = istream_byte_new(request->pool, body);

        http_server_response(request, HTTP_STATUS_OK, NULL, body);
        break;

    case MODE_FIXED:
        if (request->body != NULL)
            sink_null_new(request->body);

        http_server_response(request, HTTP_STATUS_OK, NULL,
                             istream_memory_new(request->pool, data, sizeof(data)));
        break;

    case MODE_HOLD:
        ctx->request_body = request->body != NULL
            ? istream_hold_new(request->pool, request->body)
            : NULL;

        body = istream_delayed_new(request->pool);
        async_init(&ctx->operation, &my_operation);
        async_ref_set(istream_delayed_async_ref(body), &ctx->operation);

        http_server_response(request, HTTP_STATUS_OK, NULL, body);

        static const struct timeval t;
        evtimer_add(&ctx->timer, &t);
        break;
    }
}

static void
my_error(GError *error, void *_ctx)
{
    struct context *ctx = _ctx;

    evtimer_del(&ctx->timer);
    shutdown_listener_deinit(&ctx->shutdown_listener);

    g_printerr("%s\n", error->message);
    g_error_free(error);
}

static void
my_free(void *_ctx)
{
    struct context *ctx = _ctx;

    evtimer_del(&ctx->timer);
    shutdown_listener_deinit(&ctx->shutdown_listener);
}

static const struct http_server_connection_handler handler = {
    .request = my_request,
    .error = my_error,
    .free = my_free,
};

/*
 * main
 *
 */

int main(int argc, char **argv) {
    struct context ctx;

    if (argc != 4) {
        fprintf(stderr, "Usage: %s INFD OUTFD {null|mirror|dummy|fixed|hold}\n", argv[0]);
        return EXIT_FAILURE;
    }

    const int in_fd = atoi(argv[1]);
    const int out_fd = atoi(argv[2]);

    direct_global_init();
    struct event_base *event_base = event_init();
    fb_pool_init(false);
    shutdown_listener_init(&ctx.shutdown_listener, shutdown_callback, &ctx);
    evtimer_set(&ctx.timer, timer_callback, &ctx);

    struct pool *pool = pool_new_libc(NULL, "root");

    int sockfd;
    if (in_fd != out_fd) {
        sockfd = duplex_new(pool, in_fd, out_fd);
        if (sockfd < 0) {
            perror("duplex_new() failed");
            exit(2);
        }
    } else
        sockfd = in_fd;

    const char *mode = argv[3];
    if (strcmp(mode, "null") == 0)
        ctx.mode = MODE_NULL;
    else if (strcmp(mode, "mirror") == 0)
        ctx.mode = MODE_MIRROR;
    else if (strcmp(mode, "dummy") == 0)
        ctx.mode = MODE_DUMMY;
    else if (strcmp(mode, "fixed") == 0)
        ctx.mode = MODE_FIXED;
    else if (strcmp(mode, "hold") == 0)
        ctx.mode = MODE_HOLD;
    else {
        fprintf(stderr, "Unknown mode: %s\n", mode);
        return EXIT_FAILURE;
    }

    http_server_connection_new(pool, sockfd, ISTREAM_SOCKET,
                               NULL, 0, NULL, 0,
                               true, &handler, &ctx,
                               &ctx.connection);

    event_dispatch();

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();

    fb_pool_deinit();
    event_base_free(event_base);
    direct_global_deinit();

    return EXIT_SUCCESS;
}
