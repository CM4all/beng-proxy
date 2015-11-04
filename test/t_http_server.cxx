#include "http_server/http_server.hxx"
#include "http_server/Request.hxx"
#include "http_server/Handler.hxx"
#include "http_headers.hxx"
#include "direct.hxx"
#include "pool.hxx"
#include "istream/istream.hxx"
#include "istream/istream_catch.hxx"
#include "fb_pool.hxx"
#include "system/fd_util.h"

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
                         istream_catch_new(request->pool, *request->body,
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

static constexpr HttpServerConnectionHandler catch_close_handler = {
    .request = catch_close_request,
    .log = nullptr,
    .error = catch_close_error,
    .free = catch_close_free,
};

static void
test_catch(struct pool *pool)
{
    pool = pool_new_libc(pool, "catch");

    int fds[2];
    if (socketpair_cloexec(AF_UNIX, SOCK_STREAM, 0, fds) < 0) {
        perror("socketpair()");
        abort();
    }

    static constexpr char request[] =
        "POST / HTTP/1.1\r\nContent-Length: 1024\r\n\r\nfoo";
    send(fds[1], request, sizeof(request) - 1, 0);

    HttpServerConnection *connection;
    http_server_connection_new(pool, fds[0], FdType::FD_SOCKET,
                               nullptr, nullptr,
                               nullptr, nullptr,
                               true, &catch_close_handler, nullptr,
                               &connection);
    pool_unref(pool);

    event_dispatch();

    close(fds[1]);
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
