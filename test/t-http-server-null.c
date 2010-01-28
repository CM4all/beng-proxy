#include "http-server.h"
#include "duplex.h"
#include "direct.h"
#include "sink-impl.h"

#include <stdio.h>
#include <stdlib.h>
#include <event.h>

static void
my_request(struct http_server_request *request, void *ctx,
           struct async_operation_ref *async_ref __attr_unused)
{
    (void)ctx;

    if (request->body != NULL)
        sink_null_new(request->body);

    http_server_response(request, HTTP_STATUS_NO_CONTENT, NULL, NULL);
}

static void
my_free(void *ctx)
{
    (void)ctx;
}

static const struct http_server_connection_handler handler = {
    .request = my_request,
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
                               NULL, 0,
                               "localhost", &handler, NULL,
                               &connection);

    event_dispatch();

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();

    event_base_free(event_base);
    direct_global_deinit();
}
