#include "widget-stream.h"
#include "async.h"
#include "http-response.h"
#include "child.h"

#include <inline/compiler.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <event.h>

struct context {
    struct async_operation operation;

    struct widget_stream *ws;
    struct http_response_handler_ref handler;

    bool close;

    istream_t body;
    off_t body_data;
    bool eof, abort, async_abort;
};


/*
 * istream handler
 *
 */

static size_t
my_istream_data(const void *data __attr_unused, size_t length, void *ctx)
{
    struct context *c = ctx;

    assert(c->body != NULL);

    c->body_data += length;

    if (c->close) {
        istream_close(c->body);
        return 0;
    }

    return length;
}

static void
my_istream_eof(void *ctx)
{
    struct context *c = ctx;

    assert(c->body != NULL);

    c->body = NULL;
    c->eof = true;
}

static void
my_istream_abort(void *ctx)
{
    struct context *c = ctx;

    assert(c->body != NULL);

    c->body = NULL;
    c->abort = true;
}

static const struct istream_handler my_istream_handler = {
    .data = my_istream_data,
    .eof = my_istream_eof,
    .abort = my_istream_abort,
};


/*
 * async operation
 *
 */

static struct context *
async_to_context(struct async_operation *ao)
{
    return (struct context*)(((char*)ao) - offsetof(struct context, operation));
}

static void
my_async_abort(struct async_operation *ao)
{
    struct context *c = async_to_context(ao);

    assert(!c->async_abort);

    c->async_abort = true;
}

static const struct async_operation_class my_async_operation = {
    .abort = my_async_abort,
};


/*
 * tests
 *
 */

static void
test_normal(pool_t pool, struct context *c)
{
    http_response_handler_invoke_response(&c->handler, HTTP_STATUS_OK, NULL,
                                          istream_string_new(pool, "foo"));

    pool_unref(pool);
    pool_commit();

    assert(c->body == NULL);
    assert(c->body_data == 3);
    assert(c->eof);
    assert(!c->abort);
}

static void
test_close(pool_t pool, struct context *c)
{
    c->close = true;

    http_response_handler_invoke_response(&c->handler, HTTP_STATUS_OK, NULL,
                                          istream_string_new(pool, "foo"));

    pool_unref(pool);
    pool_commit();

    assert(c->body == NULL);
    assert(c->body_data == 3);
    assert(!c->eof);
    assert(c->abort);
}


/*
 * main
 *
 */

static void
run_test(pool_t pool, void (*test)(pool_t pool, struct context *c)) {
    struct context c;

    memset(&c, 0, sizeof(c));
    async_init(&c.operation, &my_async_operation);

    pool = pool_new_linear(pool, "test", 16384);

    c.ws = widget_stream_new(pool);
    http_response_handler_set(&c.handler,
                              &widget_stream_response_handler, c.ws);
    c.body = c.ws->delayed;

    async_ref_set(widget_stream_async_ref(c.ws), &c.operation);

    istream_read(c.body);
    istream_handler_set(c.body, &my_istream_handler, &c, 0);
    istream_read(c.body);

    test(pool, &c);
    pool_commit();
}

int main(int argc, char **argv) {
    struct event_base *event_base;
    pool_t pool;

    (void)argc;
    (void)argv;

    signal(SIGPIPE, SIG_IGN);

    event_base = event_init();

    pool = pool_new_libc(NULL, "root");

    run_test(pool, test_normal);
    run_test(pool, test_close);

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();

    event_base_free(event_base);
}
