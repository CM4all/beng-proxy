#include "http-server.h"
#include "sink-impl.h"

#include <stdio.h>
#include <stdlib.h>
#include <event.h>

static void
catch_close_request(struct http_server_request *request, void *ctx,
                    struct async_operation_ref *async_ref __attr_unused)
{
    (void)ctx;

    http_server_response(request, HTTP_STATUS_OK, NULL,
                         istream_catch_new(request->pool, request->body));
    http_server_connection_close(request->connection);
}

static void
catch_close_free(void *ctx)
{
    (void)ctx;
}

static const struct http_server_connection_handler catch_close_handler = {
    .request = catch_close_request,
    .free = catch_close_free,
};

static void
test_catch(pool_t pool)
{
    istream_t request, socket;
    int fd;
    struct http_server_connection *connection;

    pool = pool_new_libc(pool, "catch");

    request = istream_cat_new(pool,
                              istream_string_new(pool,
                                                 "POST / HTTP/1.1\r\nContent-Length: 1024\r\n\r\nfoo"),
                              istream_block_new(pool),
                              NULL);
    socket = istream_socketpair_new(pool, request, &fd);
    sink_null_new(socket);

    http_server_connection_new(pool, fd,
                               "localhost", &catch_close_handler, NULL,
                               &connection);
    pool_unref(pool);

    event_dispatch();
}

int main(int argc, char **argv) {
    struct event_base *event_base;
    pool_t pool;

    (void)argc;
    (void)argv;

    event_base = event_init();

    pool = pool_new_libc(NULL, "root");

    test_catch(pool);

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();

    event_base_free(event_base);
}
