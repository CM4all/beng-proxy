#include "http-client.h"
#include "http-response.h"
#include "duplex.h"
#include "async.h"
#include "socket-util.h"
#include "growing-buffer.h"
#include "header-writer.h"
#include "lease.h"
#include "direct.h"

#include <inline/compiler.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <event.h>

static int
connect_mirror(void)
{
    int ret, sv[2];
    pid_t pid;

    ret = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    if (ret < 0) {
        perror("socketpair() failed");
        exit(EXIT_FAILURE);
    }

    pid = fork();
    if (pid < 0) {
        perror("fork() failed");
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        dup2(sv[1], 0);
        close(sv[0]);
        close(sv[1]);
        execl("./test/t-http-server-mirror", "t-http-server-mirror",
              "0", "0", NULL);
        perror("exec() failed");
        exit(EXIT_FAILURE);
    }

    close(sv[1]);

    socket_set_nonblock(sv[0], 1);

    return sv[0];
}

struct context {
    unsigned data_blocking;
    bool close_response_body_early, close_response_body_late, close_response_body_data;
    struct async_operation_ref async_ref;
    int fd;
    bool released, aborted;
    http_status_t status;

    istream_t body;
    off_t body_data;
    bool body_eof, body_abort;
};


/*
 * lease
 *
 */

static void
my_release(bool reuse __attr_unused, void *ctx)
{
    struct context *c = ctx;

    close(c->fd);
    c->fd = -1;
    c->released = true;
}

static const struct lease my_lease = {
    .release = my_release,
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

    if (c->close_response_body_early)
        istream_close(body);
    else if (body != NULL)
        istream_assign_handler(&c->body, body, &my_istream_handler, c, 0);

    if (c->close_response_body_late)
        istream_close(c->body);
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
 * tests
 *
 */

static void
test_empty(pool_t pool, struct context *c)
{
    c->fd = connect_mirror();
    http_client_request(pool, c->fd, ISTREAM_SOCKET, &my_lease, c,
                        HTTP_METHOD_GET, "/foo", NULL, NULL,
                        &my_response_handler, c, &c->async_ref);
    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->released);
    assert(c->fd < 0);
    assert(c->status == HTTP_STATUS_NO_CONTENT);
    assert(c->body == NULL);
    assert(!c->body_eof);
    assert(!c->body_abort);
}

static void
test_body(pool_t pool, struct context *c)
{
    c->fd = connect_mirror();
    http_client_request(pool, c->fd, ISTREAM_SOCKET, &my_lease, c,
                        HTTP_METHOD_GET, "/foo", NULL,
                        istream_string_new(pool, "foobar"),
                        &my_response_handler, c, &c->async_ref);
    pool_unref(pool);
    pool_commit();

    event_dispatch();

    if (c->body != NULL)
        istream_read(c->body);

    event_dispatch();

    assert(c->released);
    assert(c->status == HTTP_STATUS_OK);
    assert(c->body_eof);
    assert(c->body_data == 6);
}

static void
test_close_response_body_early(pool_t pool, struct context *c)
{
    c->close_response_body_early = true;
    c->fd = connect_mirror();
    http_client_request(pool, c->fd, ISTREAM_SOCKET, &my_lease, c,
                        HTTP_METHOD_GET, "/foo", NULL,
                        istream_string_new(pool, "foobar"),
                        &my_response_handler, c, &c->async_ref);
    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->released);
    assert(c->status == HTTP_STATUS_OK);
    assert(c->body == NULL);
    assert(c->body_data == 0);
    assert(!c->body_eof);
    assert(!c->body_abort);
}

static void
test_close_response_body_late(pool_t pool, struct context *c)
{
    c->close_response_body_late = true;
    c->fd = connect_mirror();
    http_client_request(pool, c->fd, ISTREAM_SOCKET, &my_lease, c,
                        HTTP_METHOD_GET, "/foo", NULL,
                        istream_string_new(pool, "foobar"),
                        &my_response_handler, c, &c->async_ref);
    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->released);
    assert(c->status == HTTP_STATUS_OK);
    assert(c->body == NULL);
    assert(c->body_data == 0);
    assert(!c->body_eof);
    assert(c->body_abort);
}

static void
test_close_response_body_data(pool_t pool, struct context *c)
{
    c->close_response_body_data = true;
    c->fd = connect_mirror();
    http_client_request(pool, c->fd, ISTREAM_SOCKET, &my_lease, c,
                        HTTP_METHOD_GET, "/foo", NULL,
                        istream_string_new(pool, "foobar"),
                        &my_response_handler, c, &c->async_ref);
    pool_unref(pool);
    pool_commit();

    event_dispatch();

    if (c->body != NULL)
        istream_read(c->body);

    event_dispatch();

    assert(c->released);
    assert(c->status == HTTP_STATUS_OK);
    assert(c->body == NULL);
    assert(c->body_data == 6);
    assert(!c->body_eof);
    assert(c->body_abort);
}

static void
test_close_request_body_early(pool_t pool, struct context *c)
{
    istream_t request_body = istream_block_new(pool);

    c->fd = connect_mirror();
    http_client_request(pool, c->fd, ISTREAM_SOCKET, &my_lease, c,
                        HTTP_METHOD_GET, "/foo", NULL,
                        request_body,
                        &my_response_handler, c, &c->async_ref);
    istream_close(request_body);

    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->released);
    assert(c->status == 0);
    assert(c->body == NULL);
    assert(!c->body_eof);
    assert(!c->body_abort);
}

static void
test_data_blocking(pool_t pool, struct context *c)
{
    c->data_blocking = 5;
    c->fd = connect_mirror();
    http_client_request(pool, c->fd, ISTREAM_SOCKET, &my_lease, c,
                        HTTP_METHOD_GET, "/foo", NULL,
                        istream_head_new(pool, istream_zero_new(pool), 65536),
                        &my_response_handler, c, &c->async_ref);
    pool_unref(pool);
    pool_commit();

    while (c->data_blocking > 0) {
        if (c->body != NULL)
            istream_read(c->body);
        event_loop(EVLOOP_ONCE|EVLOOP_NONBLOCK);
    }

    assert(!c->released);
    assert(c->status == HTTP_STATUS_OK);
    assert(c->body != NULL);
    assert(c->body_data > 0);
    assert(!c->body_eof);
    assert(!c->body_abort);

    istream_close(c->body);

    assert(c->released);
    assert(c->body == NULL);
    assert(!c->body_eof);
    assert(c->body_abort);
}

static void
test_body_fail(pool_t pool, struct context *c)
{
    c->fd = connect_mirror();
    http_client_request(pool, c->fd, ISTREAM_SOCKET, &my_lease, c,
                        HTTP_METHOD_GET, "/foo", NULL,
                        istream_fail_new(pool),
                        &my_response_handler, c, &c->async_ref);
    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->released);
    assert(c->aborted);
}

static void
test_head(pool_t pool, struct context *c)
{
    c->fd = connect_mirror();
    http_client_request(pool, c->fd, ISTREAM_SOCKET, &my_lease, c,
                        HTTP_METHOD_HEAD, "/foo", NULL,
                        istream_string_new(pool, "foobar"),
                        &my_response_handler, c, &c->async_ref);
    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->released);
    assert(c->fd < 0);
    assert(c->status == HTTP_STATUS_OK);
    assert(c->body == NULL);
    assert(!c->body_eof);
    assert(!c->body_abort);
}


/*
 * main
 *
 */

static void
run_test(pool_t pool, void (*test)(pool_t pool, struct context *c)) {
    struct context c;

    memset(&c, 0, sizeof(c));

    pool = pool_new_linear(pool, "test", 16384);
    test(pool, &c);
    pool_commit();
}

int main(int argc, char **argv) {
    struct event_base *event_base;
    pool_t pool;

    (void)argc;
    (void)argv;

    signal(SIGPIPE, SIG_IGN);

    direct_global_init();
    event_base = event_init();

    pool = pool_new_libc(NULL, "root");

    run_test(pool, test_empty);
    run_test(pool, test_body);
    run_test(pool, test_close_response_body_early);
    run_test(pool, test_close_response_body_late);
    run_test(pool, test_close_response_body_data);
    run_test(pool, test_close_request_body_early);
    run_test(pool, test_data_blocking);
    run_test(pool, test_body_fail);
    run_test(pool, test_head);

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();

    event_base_free(event_base);
    direct_global_deinit();
}
