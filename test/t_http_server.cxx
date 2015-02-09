#include "http_server.hxx"
#include "http_headers.hxx"
#include "sink-impl.h"
#include "direct.h"
#include "pool.hxx"
#include "istream.h"
#include "istream_block.hxx"
#include "istream_socketpair.hxx"
#include "istream-catch.h"
#include "fb_pool.hxx"

#include <stdio.h>
#include <stdlib.h>
#include <event.h>

static GError *
catch_callback(GError *error, gcc_unused void *ctx)
{
    fprintf(stderr, "%s\n", error->message);
    g_error_free(error);
    return nullptr;
}

static void
catch_close_request(struct http_server_request *request, void *ctx,
                    struct async_operation_ref *async_ref gcc_unused)
{
    (void)ctx;

    http_server_response(request, HTTP_STATUS_OK, HttpHeaders(),
                         istream_catch_new(request->pool, request->body,
                                           catch_callback, nullptr));
    http_server_connection_close(request->connection);
}

static void
catch_close_error(GError *error, void *ctx)
{
    (void)ctx;

    g_printerr("%s\n", error->message);
    g_error_free(error);
}

static void
catch_close_free(void *ctx)
{
    (void)ctx;
}

static const struct http_server_connection_handler catch_close_handler = {
    .request = catch_close_request,
    .error = catch_close_error,
    .free = catch_close_free,
};

static void
test_catch(struct pool *pool)
{
    int fd;
    struct http_server_connection *connection;

    pool = pool_new_libc(pool, "catch");

    struct istream *request =
        istream_cat_new(pool,
                        istream_string_new(pool,
                                           "POST / HTTP/1.1\r\nContent-Length: 1024\r\n\r\nfoo"),
                        istream_block_new(*pool),
                        nullptr);
    struct istream *sock = istream_socketpair_new(pool, request, &fd);
    sink_null_new(sock);

    http_server_connection_new(pool, fd, ISTREAM_SOCKET, nullptr, nullptr,
                               nullptr, 0, nullptr, 0,
                               true, &catch_close_handler, nullptr,
                               &connection);
    pool_unref(pool);

    event_dispatch();
}

int main(int argc, char **argv) {
    struct event_base *event_base;
    struct pool *pool;

    (void)argc;
    (void)argv;

    direct_global_init();
    event_base = event_init();
    fb_pool_init(false);

    pool = pool_new_libc(nullptr, "root");

    test_catch(pool);

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();

    fb_pool_deinit();
    event_base_free(event_base);
    direct_global_deinit();
}
