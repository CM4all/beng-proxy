#include "http-server.h"
#include "duplex.h"
#include "direct.h"

#include <stdio.h>
#include <stdlib.h>
#include <event.h>

static void
my_request(struct http_server_request *request, void *ctx,
           struct async_operation_ref *async_ref __attr_unused)
{
    (void)ctx;

    http_server_response(request,
                         request->body == NULL ? HTTP_STATUS_NO_CONTENT : HTTP_STATUS_OK,
                         NULL,
                         request->body);
}

static void
my_error(GError *error, void *ctx)
{
    (void)ctx;

    g_printerr("%s\n", error->message);
    g_error_free(error);
}

static void
my_free(void *ctx)
{
    (void)ctx;
}

static const struct http_server_connection_handler handler = {
    .request = my_request,
    .error = my_error,
    .free = my_free,
};

int main(int argc, char **argv) {
    struct event_base *event_base;
    pool_t pool;
    int in_fd, out_fd, sockfd;
    struct http_server_connection *connection;

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

    http_server_connection_new(pool, sockfd, ISTREAM_SOCKET,
                               NULL, 0, NULL, 0,
                               true, &handler, NULL,
                               &connection);

    event_dispatch();

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();

    event_base_free(event_base);
    direct_global_deinit();
}
