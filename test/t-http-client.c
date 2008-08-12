#include "http-client.h"
#include "http-response.h"
#include "duplex.h"
#include "async.h"
#include "socket-util.h"
#include "growing-buffer.h"
#include "header-writer.h"

#include <inline/compiler.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <event.h>

static http_client_connection_t
connect_mirror(pool_t pool,
               const struct http_client_connection_handler *handler,
               void *ctx)
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
        dup2(sv[1], 1);
        close(sv[0]);
        close(sv[1]);
        execl("./test/t-http-server-mirror", "t-http-server-mirror", NULL);
        perror("exec() failed");
        exit(EXIT_FAILURE);
    }

    close(sv[1]);

    socket_set_nonblock(sv[0], 1);

    return http_client_connection_new(pool, sv[0], handler, ctx);
}

static http_client_connection_t
connect_abort(pool_t pool,
              const struct http_client_connection_handler *handler,
              void *ctx)
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
        char buffer[256];
        ssize_t nbytes;

        close(sv[0]);
        nbytes = read(sv[1], buffer, sizeof(buffer));
        assert(nbytes > 0);

        write(sv[1], "200 OK\r\n", 8);

        shutdown(sv[1], SHUT_WR);
        sleep(1);
        exit(EXIT_FAILURE);
    }

    close(sv[1]);

    socket_set_nonblock(sv[0], 1);

    return http_client_connection_new(pool, sv[0], handler, ctx);
}

struct context {
    bool close_idle;
    bool close_early, close_late, close_data, close_abort;
    unsigned data_blocking;
    bool close_response_body_early, close_response_body_late, close_response_body_data;
    struct async_operation_ref async_ref;
    http_client_connection_t client;
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

    if (c->close_idle)
        http_client_connection_close(c->client);
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
        http_client_connection_close(c->client);
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

    if (c->close_early)
        http_client_connection_close(c->client);
    else if (c->close_response_body_early)
        istream_close(body);
    else if (body != NULL)
        istream_assign_handler(&c->body, body, &my_istream_handler, c, 0);

    if (c->close_late)
        http_client_connection_close(c->client);

    if (c->close_response_body_late)
        istream_close(c->body);
}

static void
my_response_abort(void *ctx)
{
    struct context *c = ctx;

    c->aborted = true;

    if (c->close_abort)
        http_client_connection_close(c->client);
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
    c->client = connect_mirror(pool, &my_connection_handler, c);
    http_client_request(c->client, pool, HTTP_METHOD_GET, "/foo", NULL, NULL,
                        &my_response_handler, c, &c->async_ref);

    event_dispatch();

    assert(c->client == NULL);
    assert(c->status == HTTP_STATUS_NO_CONTENT);
    assert(c->body == NULL);
    assert(!c->body_eof);
    assert(!c->body_abort);
}

static void
test_body(pool_t pool, struct context *c)
{
    c->client = connect_mirror(pool, &my_connection_handler, c);
    http_client_request(c->client, pool, HTTP_METHOD_GET, "/foo", NULL,
                        istream_string_new(pool, "foobar"),
                        &my_response_handler, c, &c->async_ref);

    event_dispatch();

    assert(c->client == NULL);
    assert(c->status == HTTP_STATUS_OK);
    assert(c->body_eof);
    assert(c->body_data == 6);
}

static void
test_close_idle(pool_t pool, struct context *c)
{
    struct growing_buffer *headers = growing_buffer_new(pool, 512);

    header_write(headers, "connection", "keep-alive");

    c->close_idle = true;
    c->client = connect_mirror(pool, &my_connection_handler, c);
    http_client_request(c->client, pool, HTTP_METHOD_GET, "/foo", headers,
                        istream_string_new(pool, "foobar"),
                        &my_response_handler, c, &c->async_ref);

    event_dispatch();

    assert(c->client == NULL);
    assert(c->status == HTTP_STATUS_OK);
    assert(c->body == NULL);
    assert(c->body_data == 6);
    assert(c->body_eof);
    assert(!c->body_abort);
    assert(c->idle);
}

static void
test_early_close(pool_t pool, struct context *c)
{
    c->client = connect_mirror(pool, &my_connection_handler, c);
    http_client_connection_close(c->client);

    assert(c->client == NULL);
    assert(c->status == 0);
}

static void
test_close_early(pool_t pool, struct context *c)
{
    c->close_early = true;
    c->client = connect_mirror(pool, &my_connection_handler, c);
    http_client_request(c->client, pool, HTTP_METHOD_GET, "/foo", NULL,
                        istream_string_new(pool, "foobar"),
                        &my_response_handler, c, &c->async_ref);

    event_dispatch();

    assert(c->client == NULL);
    assert(c->status == HTTP_STATUS_OK);
    assert(c->body == NULL);
    assert(c->body_data == 0);
    assert(!c->body_eof);
    assert(!c->body_abort);
}

static void
test_close_late(pool_t pool, struct context *c)
{
    c->close_late = true;
    c->client = connect_mirror(pool, &my_connection_handler, c);
    http_client_request(c->client, pool, HTTP_METHOD_GET, "/foo", NULL,
                        istream_string_new(pool, "foobar"),
                        &my_response_handler, c, &c->async_ref);

    event_dispatch();

    assert(c->client == NULL);
    assert(c->status == HTTP_STATUS_OK);
    assert(c->body == NULL);
    assert(c->body_data == 0);
    assert(!c->body_eof);
    assert(c->body_abort);
}

static void
test_close_data(pool_t pool, struct context *c)
{
    c->close_data = true;
    c->client = connect_mirror(pool, &my_connection_handler, c);
    http_client_request(c->client, pool, HTTP_METHOD_GET, "/foo", NULL,
                        istream_string_new(pool, "foobar"),
                        &my_response_handler, c, &c->async_ref);

    event_dispatch();

    assert(c->client == NULL);
    assert(c->status == HTTP_STATUS_OK);
    assert(c->body == NULL);
    assert(c->body_data == 6);
    assert(!c->body_eof);
    assert(c->body_abort);
}

static void
test_close_response_body_early(pool_t pool, struct context *c)
{
    c->close_response_body_early = true;
    c->client = connect_mirror(pool, &my_connection_handler, c);
    http_client_request(c->client, pool, HTTP_METHOD_GET, "/foo", NULL,
                        istream_string_new(pool, "foobar"),
                        &my_response_handler, c, &c->async_ref);

    event_dispatch();

    assert(c->client == NULL);
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
    c->client = connect_mirror(pool, &my_connection_handler, c);
    http_client_request(c->client, pool, HTTP_METHOD_GET, "/foo", NULL,
                        istream_string_new(pool, "foobar"),
                        &my_response_handler, c, &c->async_ref);

    event_dispatch();

    assert(c->client == NULL);
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
    c->client = connect_mirror(pool, &my_connection_handler, c);
    http_client_request(c->client, pool, HTTP_METHOD_GET, "/foo", NULL,
                        istream_string_new(pool, "foobar"),
                        &my_response_handler, c, &c->async_ref);

    event_dispatch();

    assert(c->client == NULL);
    assert(c->status == HTTP_STATUS_OK);
    assert(c->body == NULL);
    assert(c->body_data == 6);
    assert(!c->body_eof);
    assert(c->body_abort);
}

static void
test_data_blocking(pool_t pool, struct context *c)
{
    c->data_blocking = 5;
    c->client = connect_mirror(pool, &my_connection_handler, c);
    http_client_request(c->client, pool, HTTP_METHOD_GET, "/foo", NULL,
                        istream_head_new(pool, istream_zero_new(pool), 65536),
                        &my_response_handler, c, &c->async_ref);

    while (c->data_blocking > 0) {
        if (c->body != NULL)
            istream_read(c->body);
        event_loop(EVLOOP_ONCE|EVLOOP_NONBLOCK);
    }

    assert(c->client != NULL);
    assert(c->status == HTTP_STATUS_OK);
    assert(c->body != NULL);
    assert(c->body_data > 0);
    assert(!c->body_eof);
    assert(!c->body_abort);

    http_client_connection_close(c->client);

    assert(c->client == NULL);
    assert(c->body == NULL);
    assert(!c->body_eof);
    assert(c->body_abort);
}

static void
test_socket_close(pool_t pool, struct context *c)
{
    c->close_abort = true;
    c->client = connect_abort(pool, &my_connection_handler, c);
    http_client_request(c->client, pool, HTTP_METHOD_GET, "/foo", NULL, NULL,
                        &my_response_handler, c, &c->async_ref);

    event_dispatch();

    assert(c->client == NULL);
    assert(c->body == NULL);
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
    pool_unref(pool);
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

    run_test(pool, test_empty);
    run_test(pool, test_body);
    run_test(pool, test_close_idle);
    run_test(pool, test_early_close);
    run_test(pool, test_close_early);
    run_test(pool, test_close_late);
    run_test(pool, test_close_data);
    run_test(pool, test_close_response_body_early);
    run_test(pool, test_close_response_body_late);
    run_test(pool, test_close_response_body_data);
    run_test(pool, test_data_blocking);
    run_test(pool, test_socket_close);

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();

    event_base_free(event_base);
}
