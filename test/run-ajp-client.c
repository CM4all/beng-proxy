#include "ajp-client.h"
#include "http-client.h"
#include "http-response.h"
#include "async.h"
#include "socket-util.h"
#include "lease.h"

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

struct context {
    int fd;
    bool idle, reuse, aborted;
    http_status_t status;

    istream_t body;
    bool body_eof, body_abort;
};


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

static size_t
my_istream_data(const void *data, size_t length, void *ctx)
{
    struct context *c = ctx;
    ssize_t nbytes;

    nbytes = write(1, data, length);
    if (nbytes <= 0) {
        istream_close(c->body);
        return 0;
    }

    return (size_t)nbytes;
}

static void
my_istream_eof(void *ctx)
{
    struct context *c = ctx;

    c->body = NULL;
    c->body_eof = true;
}

static void
my_istream_abort(void *ctx)
{
    struct context *c = ctx;

    c->body = NULL;
    c->body_abort = true;
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

    if (body != NULL)
        istream_assign_handler(&c->body, body, &my_istream_handler, c, 0);
    else
        c->body_eof = true;
}

static void
my_response_abort(void *ctx)
{
    struct context *c = ctx;

    c->aborted = true;
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
    http_method_t method;
    istream_t request_body;
    static struct context ctx;
    struct async_operation_ref async_ref;

    if (argc < 3 || argc > 4) {
        fprintf(stderr, "usage: run-ajp-client HOST[:PORT] URI [BODY]\n");
        return 1;
    }

    /* connect socket */

    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;

    ret = socket_resolve_host_port(argv[1], 8009, &hints, &ai);
    if (ret != 0) {
        fprintf(stderr, "Failed to resolve host name\n");
        return 2;
    }

    fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0) {
        fprintf(stderr, "socket() failed: %s\n", strerror(errno));
        return 2;
    }

    ret = connect(fd, ai->ai_addr, ai->ai_addrlen);
    if (ret < 0) {
        fprintf(stderr, "connect() failed: %s\n", strerror(errno));
        return 2;
    }

    freeaddrinfo(ai);

    socket_set_nonblock(fd, true);
    socket_set_nodelay(fd, true);

    /* initialize */

    signal(SIGPIPE, SIG_IGN);

    event_base = event_init();

    root_pool = pool_new_libc(NULL, "root");
    pool = pool_new_linear(root_pool, "test", 8192);

    /* open request body */

    if (argc >= 4) {
        struct stat st;

        ret = stat(argv[3], &st);
        if (ret < 0) {
            fprintf(stderr, "Failed to stat %s: %s\n",
                    argv[3], strerror(errno));
            return 2;
        }

        method = HTTP_METHOD_POST;
        request_body = istream_file_new(pool, argv[3], st.st_size);
    } else {
        method = HTTP_METHOD_GET;
        request_body = NULL;
    }

    /* run test */

    ajp_client_request(pool, fd, &ajp_socket_lease, &ctx,
                       "http", "127.0.0.1", "localhost",
                       "localhost", 80, false,
                       method, argv[2], NULL, request_body,
                       &my_response_handler, &ctx,
                       &async_ref);

    event_dispatch();

    assert(ctx.body_eof || ctx.body_abort || ctx.aborted);

    fprintf(stderr, "reuse=%d\n", ctx.reuse);

    /* cleanup */

    pool_unref(pool);
    pool_commit();

    pool_unref(root_pool);
    pool_commit();
    pool_recycler_clear();

    event_base_free(event_base);

    return ctx.body_eof ? 0 : 2;
}
