#include "http-server.h"
#include "duplex.h"
#include "direct.h"
#include "async.h"

#include <stdio.h>
#include <stdlib.h>
#include <event.h>

struct context {
    struct async_operation operation;

    struct http_server_connection *connection;

    struct istream *request_body;

    struct event timer;
};

static void
timer_callback(G_GNUC_UNUSED int fd, G_GNUC_UNUSED short event,
                           void *_ctx)
{
    struct context *ctx = _ctx;

    http_server_connection_close(ctx->connection);
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
           struct async_operation_ref *async_ref __attr_unused)
{
    struct context *ctx = _ctx;

    ctx->request_body = request->body;

    struct istream *response_body = istream_delayed_new(request->pool);
    async_init(&ctx->operation, &my_operation);
    async_ref_set(istream_delayed_async_ref(response_body), &ctx->operation);

    http_server_response(request, HTTP_STATUS_OK, NULL, response_body);

    evtimer_set(&ctx->timer, timer_callback, ctx);
    static const struct timeval t;
    evtimer_add(&ctx->timer, &t);
}

static void
my_free(void *ctx)
{
    (void)ctx;
}

static const struct http_server_connection_handler my_handler = {
    .request = my_request,
    .free = my_free,
};

/*
 * main
 *
 */

int main(int argc, char **argv) {
    struct event_base *event_base;
    pool_t pool;
    int in_fd, out_fd, sockfd;

    if (argc != 3) {
        fprintf(stderr, "Usage: %s INFD OUTFD\n", argv[0]);
        return 1;
    }

    in_fd = atoi(argv[1]);
    out_fd = atoi(argv[2]);

    direct_global_init();
    event_base = event_init();

    pool = pool_new_libc(NULL, "root");

    if (in_fd != out_fd) {
        sockfd = duplex_new(pool, in_fd, out_fd);
        if (sockfd < 0) {
            perror("duplex_new() failed");
            exit(2);
        }
    } else
        sockfd = in_fd;

    struct context context;

    http_server_connection_new(pool, sockfd, ISTREAM_SOCKET,
                               NULL, 0,
                               "localhost", &my_handler, &context,
                               &context.connection);

    event_dispatch();

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();

    event_base_free(event_base);
    direct_global_deinit();
}
