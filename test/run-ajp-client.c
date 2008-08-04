#include "ajp-client.h"
#include "http-client.h"
#include "http-response.h"
#include "async.h"
#include "socket-util.h"

#include <inline/compiler.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <event.h>
#include <netdb.h>

struct context {
    bool close_early, close_late, close_data, close_abort;
    unsigned data_blocking;
    bool close_response_body_early, close_response_body_late, close_response_body_data;
    struct async_operation_ref async_ref;
    struct ajp_connection *client;
    bool idle, aborted;
    http_status_t status;

    istream_t body;
    off_t body_data;
    bool body_eof, body_abort;
};


/*
 * http_client_connection_handler
 *
 */

static void
my_connection_idle(void *ctx)
{
    struct context *c = ctx;

    c->idle = true;
}

static void
my_connection_free(void *ctx __attr_unused)
{
    struct context *c = ctx;

    c->client = NULL;
}

static const struct http_client_connection_handler my_connection_handler = {
    .idle = my_connection_idle,
    .free = my_connection_free,
};


/*
 * istream handler
 *
 */

static size_t
my_istream_data(const void *data __attr_unused, size_t length, void *ctx)
{
    struct context *c = ctx;

    c->body_data += length;

    if (c->close_data) {
        ajp_connection_close(c->client);
        return 0;
    }

    if (c->close_response_body_data) {
        istream_close(c->body);
        return 0;
    }

    if (c->data_blocking) {
        --c->data_blocking;
        return 0;
    }

    return length;
}

static void
my_istream_eof(void *ctx)
{
    struct context *c = ctx;

    c->body = NULL;
    c->body_eof = true;

    ajp_connection_close(c->client);
}

static void
my_istream_abort(void *ctx)
{
    struct context *c = ctx;

    c->body = NULL;
    c->body_abort = true;

    ajp_connection_close(c->client);
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
my_response(http_status_t status, struct strmap *headers __attr_unused,
            istream_t body __attr_unused,
            void *ctx)
{
    struct context *c = ctx;

    c->status = status;

    if (c->close_early)
        ajp_connection_close(c->client);
    else if (c->close_response_body_early)
        istream_close(body);
    else if (body != NULL)
        istream_assign_handler(&c->body, body, &my_istream_handler, c, 0);

    if (c->close_late)
        ajp_connection_close(c->client);

    if (c->close_response_body_late)
        istream_close(c->body);
}

static void
my_response_abort(void *ctx)
{
    struct context *c = ctx;

    c->aborted = true;

    if (c->close_abort)
        ajp_connection_close(c->client);
}

static const struct http_response_handler my_response_handler = {
    .response = my_response,
    .abort = my_response_abort,
};


/*
 * main
 *
 */

int main(int argc, char **argv) {
    int fd, ret;
    struct addrinfo hints, *ai;
    struct event_base *event_base;
    pool_t root_pool, pool;
    static struct context ctx;
    struct async_operation_ref async_ref;

    (void)argc;
    (void)argv;

    /* connect socket */

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = PF_INET;
    hints.ai_socktype = SOCK_STREAM;

    ret = getaddrinfo("cfatest01", "8009", &hints, &ai);
    assert(ret == 0);

    fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);

    ret = connect(fd, ai->ai_addr, ai->ai_addrlen);
    assert(ret == 0);

    socket_set_nonblock(fd, 1);
    socket_set_nodelay(fd, 1);

    /* initialize */

    signal(SIGPIPE, SIG_IGN);

    event_base = event_init();

    root_pool = pool_new_libc(NULL, "root");
    pool = pool_new_linear(root_pool, "test", 8192);

    /* run test */

    ctx.client = ajp_new(pool, fd, &my_connection_handler, &ctx);
    ajp_request(ctx.client, pool,
                HTTP_METHOD_GET, "/cm4all-bulldog-butch/", NULL, NULL,
                &my_response_handler, &ctx,
                &async_ref);

    event_dispatch();

    assert(ctx.body_data > 0);
    assert(ctx.body_eof);
    assert(!ctx.body_abort);

    /* cleanup */

    pool_unref(pool);
    pool_commit();

    pool_unref(root_pool);
    pool_commit();
    pool_recycler_clear();

    event_base_free(event_base);
}
