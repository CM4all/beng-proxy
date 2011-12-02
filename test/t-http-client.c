#include "http-client.h"
#include "http-response.h"
#include "duplex.h"
#include "async.h"
#include "socket-util.h"
#include "growing-buffer.h"
#include "header-writer.h"
#include "lease.h"
#include "direct.h"
#include "fd_util.h"
#include "strmap.h"

#include <inline/compiler.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <event.h>

static inline GQuark
test_quark(void)
{
    return g_quark_from_static_string("test");
}

static int
connect_server(const char *path)
{
    int ret, sv[2];
    pid_t pid;

    ret = socketpair_cloexec(AF_UNIX, SOCK_STREAM, 0, sv);
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
        execl(path, path,
              "0", "0", NULL);
        perror("exec() failed");
        exit(EXIT_FAILURE);
    }

    close(sv[1]);

    socket_set_nonblock(sv[0], 1);

    return sv[0];
}

static int
connect_mirror(void)
{
    return connect_server("./test/t-http-server-mirror");
}

static int
connect_null(void)
{
    return connect_server("./test/t-http-server-null");
}

static int
connect_dummy(void)
{
    return connect_server("./test/t-http-server-dummy");
}

static int
connect_fixed(void)
{
    return connect_server("./test/t-http-server-fixed");
}

struct context {
    pool_t pool;

    unsigned data_blocking;
    bool close_response_body_early, close_response_body_late, close_response_body_data;
    bool response_body_byte;
    struct async_operation_ref async_ref;
    int fd;
    bool released, aborted;
    http_status_t status;
    GError *request_error;

    char *content_length;
    off_t available;

    istream_t delayed;

    istream_t body;
    off_t body_data, consumed_body_data;
    bool body_eof, body_abort, body_closed;

    istream_t request_body;
    bool close_request_body_early, close_request_body_eof;
    GError *body_error;
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
        c->body_closed = true;
        istream_free_handler(&c->body);
        return 0;
    }

    if (c->data_blocking) {
        --c->data_blocking;
        return 0;
    }

    c->consumed_body_data += length;
    return length;
}

static void
my_istream_eof(void *ctx)
{
    struct context *c = ctx;

    c->body = NULL;
    c->body_eof = true;

    if (c->close_request_body_eof) {
        GError *error = g_error_new_literal(test_quark(), 0,
                                            "close_request_body_eof");
        istream_delayed_set_abort(c->request_body, error);
    }
}

static void
my_istream_abort(GError *error, void *ctx)
{
    struct context *c = ctx;

    c->body = NULL;
    c->body_abort = true;

    assert(c->body_error == NULL);
    c->body_error = error;
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
my_response(http_status_t status, struct strmap *headers, istream_t body,
            void *ctx)
{
    struct context *c = ctx;

    c->status = status;
    const char *content_length =
        strmap_get_checked(headers, "content-length");
    if (content_length != NULL)
        c->content_length = strdup(content_length);
    c->available = body != NULL
        ? istream_available(body, false)
        : -2;

    if (c->close_request_body_early) {
        GError *error = g_error_new_literal(test_quark(), 0,
                                            "close_request_body_early");
        istream_delayed_set_abort(c->request_body, error);
    }

    if (c->response_body_byte) {
        assert(body != NULL);
        body = istream_byte_new(c->pool, body);
    }

    if (c->close_response_body_early)
        istream_close_unused(body);
    else if (body != NULL)
        istream_assign_handler(&c->body, body, &my_istream_handler, c, 0);

    if (c->close_response_body_late) {
        c->body_closed = true;
        istream_free_handler(&c->body);
    }

    if (c->delayed != NULL) {
        GError *error = g_error_new_literal(test_quark(), 0,
                                            "delayed_fail");
        istream_delayed_set(c->delayed, istream_fail_new(c->pool, error));
        istream_read(c->delayed);
    }
}

static void
my_response_abort(GError *error, void *ctx)
{
    struct context *c = ctx;

    assert(c->request_error == NULL);
    c->request_error = error;

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
    assert(c->content_length == NULL);
    assert(c->body == NULL);
    assert(!c->body_eof);
    assert(!c->body_abort);
    assert(c->request_error == NULL);
    assert(c->body_error == NULL);
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
    assert(c->content_length == NULL);
    assert(c->available == 6);
    assert(c->body_eof);
    assert(c->body_data == 6);
    assert(c->request_error == NULL);
    assert(c->body_error == NULL);
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
    assert(c->content_length == NULL);
    assert(c->available == 6);
    assert(c->body == NULL);
    assert(c->body_data == 0);
    assert(!c->body_eof);
    assert(!c->body_abort);
    assert(c->request_error == NULL);
    assert(c->body_error == NULL);
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
    assert(c->content_length == NULL);
    assert(c->available == 6);
    assert(c->body == NULL);
    assert(c->body_data == 0);
    assert(!c->body_eof);
    assert(c->body_abort || c->body_closed);
    assert(c->request_error == NULL);
    assert(c->body_error == NULL);
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
    assert(c->content_length == NULL);
    assert(c->available == 6);
    assert(c->body == NULL);
    assert(c->body_data == 6);
    assert(!c->body_eof);
    assert(!c->body_abort);
    assert(c->body_closed);
    assert(c->request_error == NULL);
    assert(c->body_error == NULL);
}

static void
test_close_request_body_early(pool_t pool, struct context *c)
{
    istream_t request_body = istream_delayed_new(pool);

    c->fd = connect_mirror();
    http_client_request(pool, c->fd, ISTREAM_SOCKET, &my_lease, c,
                        HTTP_METHOD_GET, "/foo", NULL,
                        request_body,
                        &my_response_handler, c, &c->async_ref);

    GError *error = g_error_new_literal(test_quark(), 0,
                                        "fail_request_body_early");
    istream_delayed_set_abort(request_body, error);

    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->released);
    assert(c->status == 0);
    assert(c->body == NULL);
    assert(!c->body_eof);
    assert(!c->body_abort);
    assert(c->body_error == NULL);
    assert(c->request_error == error);
    g_error_free(error);
}

static void
test_close_request_body_fail(pool_t pool, struct context *c)
{
    istream_t delayed = istream_delayed_new(pool);
    istream_t request_body =
        istream_cat_new(pool,
                        istream_head_new(pool, istream_zero_new(pool), 8192),
                        delayed,
                        NULL);

    c->delayed = delayed;
    c->fd = connect_mirror();
    http_client_request(pool, c->fd, ISTREAM_SOCKET, &my_lease, c,
                        HTTP_METHOD_GET, "/foo", NULL,
                        request_body,
                        &my_response_handler, c, &c->async_ref);
    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->released);
    assert(c->status == 200);
    assert(c->content_length == NULL);
    assert(c->available == -1);
    assert(c->body == NULL);
    assert(!c->body_eof);
    assert(c->body_abort);

    if (c->body_error != NULL && c->request_error == NULL) {
        c->request_error = c->body_error;
        c->body_error = NULL;
    }

    assert(c->request_error != NULL);
    assert(strcmp(c->request_error->message, "delayed_fail") == 0);
    g_error_free(c->request_error);
    assert(c->body_error == NULL);
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
    assert(c->content_length == NULL);
    assert(c->available == -1);
    assert(c->body != NULL);
    assert(c->body_data > 0);
    assert(!c->body_eof);
    assert(!c->body_abort);
    assert(c->request_error == NULL);
    assert(c->body_error == NULL);

    istream_close_handler(c->body);

    assert(c->released);
    assert(!c->body_eof);
    assert(!c->body_abort);
    assert(c->request_error == NULL);
    assert(c->body_error == NULL);
}

/**
 * This produces a closed socket while the HTTP client has data left
 * in the buffer.
 */
static void
test_data_blocking2(pool_t pool, struct context *c)
{
    struct growing_buffer *request_headers;

    request_headers = growing_buffer_new(pool, 1024);
    header_write(request_headers, "connection", "close");

    c->response_body_byte = true;
    c->fd = connect_mirror();
    http_client_request(pool, c->fd, ISTREAM_SOCKET, &my_lease, c,
                        HTTP_METHOD_GET, "/foo", request_headers,
                        istream_head_new(pool, istream_zero_new(pool), 256),
                        &my_response_handler, c, &c->async_ref);
    pool_unref(pool);
    pool_commit();

    if (c->body != NULL)
        istream_read(c->body);
    event_dispatch();

    /* the socket is released by now, but the body isn't finished
       yet */
    assert(c->released);
    assert(c->status == HTTP_STATUS_OK);
    assert(c->content_length == NULL);
    assert(c->available == -1);
    assert(c->body != NULL);
    assert(!c->body_eof);
    assert(!c->body_abort);
    assert(c->consumed_body_data < 256);
    assert(c->request_error == NULL);
    assert(c->body_error == NULL);

    /* receive the rest of the response body from the buffer */
    while (c->body != NULL) {
        istream_read(c->body);
        event_loop(EVLOOP_ONCE|EVLOOP_NONBLOCK);
    }

    assert(c->released);
    assert(c->body_eof);
    assert(!c->body_abort);
    assert(c->consumed_body_data == 256);
    assert(c->request_error == NULL);
    assert(c->body_error == NULL);
}

static void
test_body_fail(pool_t pool, struct context *c)
{
    c->fd = connect_mirror();

    GError *error = g_error_new_literal(test_quark(), 0,
                                        "body_fail");

    http_client_request(pool, c->fd, ISTREAM_SOCKET, &my_lease, c,
                        HTTP_METHOD_GET, "/foo", NULL,
                        istream_fail_new(pool, error),
                        &my_response_handler, c, &c->async_ref);
    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->released);
    assert(c->aborted || c->body_abort);

    if (c->body_error != NULL && c->request_error == NULL) {
        c->request_error = c->body_error;
        c->body_error = NULL;
    }

    assert(c->request_error == error);
    g_error_free(error);
    assert(c->body_error == NULL);
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
    assert(c->content_length != NULL);
    assert(strcmp(c->content_length, "6") == 0);
    free(c->content_length);
    assert(c->body == NULL);
    assert(!c->body_eof);
    assert(!c->body_abort);
    assert(c->request_error == NULL);
    assert(c->body_error == NULL);
}

static void
test_ignored_body(pool_t pool, struct context *c)
{
    c->fd = connect_null();
    http_client_request(pool, c->fd, ISTREAM_SOCKET, &my_lease, c,
                        HTTP_METHOD_GET, "/foo", NULL,
                        istream_zero_new(pool),
                        &my_response_handler, c, &c->async_ref);
    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->released);
    assert(c->fd < 0);
    assert(c->status == HTTP_STATUS_NO_CONTENT);
    assert(c->content_length == NULL);
    assert(c->body == NULL);
    assert(!c->body_eof);
    assert(!c->body_abort);
    assert(c->request_error == NULL);
    assert(c->body_error == NULL);
}

/**
 * Close request body in the response handler (with response body).
 */
static void
test_close_ignored_request_body(pool_t pool, struct context *c)
{
    c->fd = connect_null();
    c->close_request_body_early = true;
    http_client_request(pool, c->fd, ISTREAM_SOCKET, &my_lease, c,
                        HTTP_METHOD_GET, "/foo", NULL,
                        c->request_body = istream_delayed_new(pool),
                        &my_response_handler, c, &c->async_ref);
    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->released);
    assert(c->fd < 0);
    assert(c->status == HTTP_STATUS_NO_CONTENT);
    assert(c->content_length == NULL);
    assert(c->body == NULL);
    assert(!c->body_eof);
    assert(!c->body_abort);
    assert(c->request_error == NULL);
    assert(c->body_error == NULL);
}

/**
 * Close request body in the response handler, method HEAD (no
 * response body).
 */
static void
test_head_close_ignored_request_body(pool_t pool, struct context *c)
{
    c->fd = connect_null();
    c->close_request_body_early = true;
    http_client_request(pool, c->fd, ISTREAM_SOCKET, &my_lease, c,
                        HTTP_METHOD_HEAD, "/foo", NULL,
                        c->request_body = istream_delayed_new(pool),
                        &my_response_handler, c, &c->async_ref);
    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->released);
    assert(c->fd < 0);
    assert(c->status == HTTP_STATUS_NO_CONTENT);
    assert(c->content_length == NULL);
    assert(c->body == NULL);
    assert(!c->body_eof);
    assert(!c->body_abort);
    assert(c->request_error == NULL);
    assert(c->body_error == NULL);
}

/**
 * Close request body in the response_eof handler.
 */
static void
test_close_request_body_eor(pool_t pool, struct context *c)
{
    c->fd = connect_dummy();
    c->close_request_body_eof = true;
    http_client_request(pool, c->fd, ISTREAM_SOCKET, &my_lease, c,
                        HTTP_METHOD_GET, "/foo", NULL,
                        c->request_body = istream_delayed_new(pool),
                        &my_response_handler, c, &c->async_ref);
    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->released);
    assert(c->fd < 0);
    assert(c->status == HTTP_STATUS_OK);
    assert(c->content_length == NULL);
    assert(c->body == NULL);
    assert(c->body_eof);
    assert(!c->body_abort);
    assert(c->request_error == NULL);
    assert(c->body_error == NULL);
}

/**
 * Close request body in the response_eof handler.
 */
static void
test_close_request_body_eor2(pool_t pool, struct context *c)
{
    c->fd = connect_fixed();
    c->close_request_body_eof = true;
    http_client_request(pool, c->fd, ISTREAM_SOCKET, &my_lease, c,
                        HTTP_METHOD_GET, "/foo", NULL,
                        c->request_body = istream_delayed_new(pool),
                        &my_response_handler, c, &c->async_ref);
    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->released);
    assert(c->fd < 0);
    assert(c->status == HTTP_STATUS_OK);
    assert(c->content_length == NULL);
    assert(c->body == NULL);
    assert(c->body_eof);
    assert(!c->body_abort);
    assert(c->request_error == NULL);
    assert(c->body_error == NULL);
}


/*
 * main
 *
 */

static void
run_test(pool_t pool, void (*test)(pool_t pool, struct context *c)) {
    struct context c;

    memset(&c, 0, sizeof(c));
    c.pool = pool_new_linear(pool, "test", 16384);
    test(c.pool, &c);
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
    run_test(pool, test_close_request_body_fail);
    run_test(pool, test_data_blocking);
    run_test(pool, test_data_blocking2);
    run_test(pool, test_body_fail);
    run_test(pool, test_head);
    run_test(pool, test_ignored_body);
    run_test(pool, test_close_ignored_request_body);
    run_test(pool, test_head_close_ignored_request_body);
    run_test(pool, test_close_request_body_eor);
    run_test(pool, test_close_request_body_eor2);

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();

    event_base_free(event_base);
    direct_global_deinit();
}
