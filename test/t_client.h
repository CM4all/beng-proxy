#include "http-response.h"
#include "async.h"
#include "lease.h"
#include "istream.h"
#include "strmap.h"

#ifdef HAVE_EXPECT_100
#include "http-client.h"
#endif

#include <inline/compiler.h>
#include <http/method.h>

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

struct connection;

static struct connection *
connect_mirror(void);

static struct connection *
connect_null(void);

gcc_unused
static struct connection *
connect_dummy(void);

gcc_unused
static struct connection *
connect_fixed(void);

#ifdef HAVE_EXPECT_100
static struct connection *
connect_twice_100(void);
#endif

static struct connection *
connect_hold(void);

static void
connection_close(struct connection *c);

static void
client_request(struct pool *pool, struct connection *connection,
               const struct lease *lease, void *lease_ctx,
               http_method_t method, const char *uri,
               struct strmap *headers, struct istream *body,
#ifdef HAVE_EXPECT_100
               bool expect_100,
#endif
               const struct http_response_handler *handler,
               void *ctx,
               struct async_operation_ref *async_ref);

struct context {
    struct pool *pool;

    unsigned data_blocking;
    bool close_response_body_early, close_response_body_late, close_response_body_data;
    bool response_body_byte;
    struct async_operation_ref async_ref;
    struct connection *connection;
    bool released, aborted;
    http_status_t status;
    GError *request_error;

    char *content_length;
    off_t available;

    struct istream *delayed;

    struct istream *body;
    off_t body_data, consumed_body_data;
    bool body_eof, body_abort, body_closed;

    struct istream *request_body;
    bool close_request_body_early, close_request_body_eof;
    GError *body_error;
};


/*
 * lease
 *
 */

static void
my_release(bool reuse gcc_unused, void *ctx)
{
    struct context *c = ctx;
    assert(c->connection != NULL);

    connection_close(c->connection);
    c->connection = NULL;
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
my_istream_data(const void *data gcc_unused, size_t length, void *ctx)
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
my_response(http_status_t status, struct strmap *headers, struct istream *body,
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
test_empty(struct pool *pool, struct context *c)
{
    c->connection = connect_mirror();
    client_request(pool, c->connection, &my_lease, c,
                   HTTP_METHOD_GET, "/foo", NULL, NULL,
#ifdef HAVE_EXPECT_100
                   false,
#endif
                   &my_response_handler, c, &c->async_ref);
    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->released);
    assert(c->connection == NULL);
    assert(c->status == HTTP_STATUS_NO_CONTENT);
    assert(c->content_length == NULL);
    assert(c->body == NULL);
    assert(!c->body_eof);
    assert(!c->body_abort);
    assert(c->request_error == NULL);
    assert(c->body_error == NULL);
}

static void
test_body(struct pool *pool, struct context *c)
{
    c->connection = connect_mirror();
    client_request(pool, c->connection, &my_lease, c,
                   HTTP_METHOD_GET, "/foo", NULL,
                   istream_string_new(pool, "foobar"),
#ifdef HAVE_EXPECT_100
                   false,
#endif
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
test_close_response_body_early(struct pool *pool, struct context *c)
{
    c->close_response_body_early = true;
    c->connection = connect_mirror();
    client_request(pool, c->connection, &my_lease, c,
                   HTTP_METHOD_GET, "/foo", NULL,
                   istream_string_new(pool, "foobar"),
#ifdef HAVE_EXPECT_100
                   false,
#endif
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
test_close_response_body_late(struct pool *pool, struct context *c)
{
    c->close_response_body_late = true;
    c->connection = connect_mirror();
    client_request(pool, c->connection, &my_lease, c,
                   HTTP_METHOD_GET, "/foo", NULL,
                   istream_string_new(pool, "foobar"),
#ifdef HAVE_EXPECT_100
                   false,
#endif
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
test_close_response_body_data(struct pool *pool, struct context *c)
{
    c->close_response_body_data = true;
    c->connection = connect_mirror();
    client_request(pool, c->connection, &my_lease, c,
                   HTTP_METHOD_GET, "/foo", NULL,
                   istream_string_new(pool, "foobar"),
#ifdef HAVE_EXPECT_100
                   false,
#endif
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

static struct istream *
wrap_fake_request_body(gcc_unused struct pool *pool, struct istream *i)
{
#ifndef HAVE_CHUNKED_REQUEST_BODY
    if (istream_available(i, false) < 0)
        i = istream_head_new(pool, i, 8192, true);
#endif
    return i;
}

static void
test_close_request_body_early(struct pool *pool, struct context *c)
{
    struct istream *request_body = istream_delayed_new(pool);

    c->connection = connect_mirror();
    client_request(pool, c->connection, &my_lease, c,
                   HTTP_METHOD_GET, "/foo", NULL,
                   wrap_fake_request_body(pool, request_body),
#ifdef HAVE_EXPECT_100
                   false,
#endif
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
test_close_request_body_fail(struct pool *pool, struct context *c)
{
    struct istream *delayed = istream_delayed_new(pool);
    struct istream *request_body =
        istream_cat_new(pool,
                        istream_head_new(pool, istream_zero_new(pool),
                                         4096, false),
                        delayed,
                        NULL);

    c->delayed = delayed;
    c->connection = connect_mirror();
    client_request(pool, c->connection, &my_lease, c,
                   HTTP_METHOD_GET, "/foo", NULL,
                   wrap_fake_request_body(pool, request_body),
#ifdef HAVE_EXPECT_100
                   false,
#endif
                   &my_response_handler, c, &c->async_ref);
    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->released);
    assert(c->status == 200);
    assert(c->content_length == NULL);
#ifdef HAVE_CHUNKED_REQUEST_BODY
    assert(c->available == -1);
#else
    assert(c->available == 8192);
#endif
    assert(c->body == NULL);
    assert(!c->body_eof);
    assert(c->body_abort);

    if (c->body_error != NULL && c->request_error == NULL) {
        c->request_error = c->body_error;
        c->body_error = NULL;
    }

    assert(c->request_error != NULL);
    assert(strstr(c->request_error->message, "delayed_fail") != 0);
    g_error_free(c->request_error);
    assert(c->body_error == NULL);
}

static void
test_data_blocking(struct pool *pool, struct context *c)
{
    struct istream *request_body =
        istream_head_new(pool, istream_zero_new(pool), 65536, false);

    c->data_blocking = 5;
    c->connection = connect_mirror();
    client_request(pool, c->connection, &my_lease, c,
                   HTTP_METHOD_GET, "/foo", NULL,
                   wrap_fake_request_body(pool, request_body),
#ifdef HAVE_EXPECT_100
                   false,
#endif
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
#ifdef HAVE_CHUNKED_REQUEST_BODY
    assert(c->available == -1);
#else
    assert(c->available == 8192);
#endif
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
test_data_blocking2(struct pool *pool, struct context *c)
{
    struct strmap *request_headers = strmap_new(pool, 7);
    strmap_add(request_headers, "connection", "close");

    c->response_body_byte = true;
    c->connection = connect_mirror();
    client_request(pool, c->connection, &my_lease, c,
                   HTTP_METHOD_GET, "/foo", request_headers,
                   istream_head_new(pool, istream_zero_new(pool), 256, true),
#ifdef HAVE_EXPECT_100
                   false,
#endif
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
    assert(c->available == 256);
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
test_body_fail(struct pool *pool, struct context *c)
{
    c->connection = connect_mirror();

    GError *error = g_error_new_literal(test_quark(), 0,
                                        "body_fail");

    client_request(pool, c->connection, &my_lease, c,
                   HTTP_METHOD_GET, "/foo", NULL,
                   wrap_fake_request_body(pool, istream_fail_new(pool, error)),
#ifdef HAVE_EXPECT_100
                   false,
#endif
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
test_head(struct pool *pool, struct context *c)
{
    c->connection = connect_mirror();
    client_request(pool, c->connection, &my_lease, c,
                   HTTP_METHOD_HEAD, "/foo", NULL,
                   istream_string_new(pool, "foobar"),
#ifdef HAVE_EXPECT_100
                   false,
#endif
                   &my_response_handler, c, &c->async_ref);
    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->released);
    assert(c->connection == NULL);
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
test_ignored_body(struct pool *pool, struct context *c)
{
    c->connection = connect_null();
    client_request(pool, c->connection, &my_lease, c,
                   HTTP_METHOD_GET, "/foo", NULL,
                   wrap_fake_request_body(pool, istream_zero_new(pool)),
#ifdef HAVE_EXPECT_100
                   false,
#endif
                   &my_response_handler, c, &c->async_ref);
    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->released);
    assert(c->connection == NULL);
    assert(c->status == HTTP_STATUS_NO_CONTENT);
    assert(c->content_length == NULL);
    assert(c->body == NULL);
    assert(!c->body_eof);
    assert(!c->body_abort);
    assert(c->request_error == NULL);
    assert(c->body_error == NULL);
}

#ifdef ENABLE_CLOSE_IGNORED_REQUEST_BODY

/**
 * Close request body in the response handler (with response body).
 */
static void
test_close_ignored_request_body(struct pool *pool, struct context *c)
{
    c->connection = connect_null();
    c->request_body = istream_delayed_new(pool);
    c->close_request_body_early = true;
    client_request(pool, c->connection, &my_lease, c,
                   HTTP_METHOD_GET, "/foo", NULL,
                   wrap_fake_request_body(pool, c->request_body),
#ifdef HAVE_EXPECT_100
                   false,
#endif
                   &my_response_handler, c, &c->async_ref);
    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->released);
    assert(c->connection == NULL);
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
test_head_close_ignored_request_body(struct pool *pool, struct context *c)
{
    c->connection = connect_null();
    c->request_body = istream_delayed_new(pool);
    c->close_request_body_early = true;
    client_request(pool, c->connection, &my_lease, c,
                   HTTP_METHOD_HEAD, "/foo", NULL,
                   wrap_fake_request_body(pool, c->request_body),
#ifdef HAVE_EXPECT_100
                   false,
#endif
                   &my_response_handler, c, &c->async_ref);
    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->released);
    assert(c->connection == NULL);
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
test_close_request_body_eor(struct pool *pool, struct context *c)
{
    c->connection = connect_dummy();
    c->close_request_body_eof = true;
    c->request_body = istream_delayed_new(pool);
    client_request(pool, c->connection, &my_lease, c,
                   HTTP_METHOD_GET, "/foo", NULL,
                   wrap_fake_request_body(pool, c->request_body),
#ifdef HAVE_EXPECT_100
                   false,
#endif
                   &my_response_handler, c, &c->async_ref);
    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->released);
    assert(c->connection == NULL);
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
test_close_request_body_eor2(struct pool *pool, struct context *c)
{
    c->connection = connect_fixed();
    c->close_request_body_eof = true;
    client_request(pool, c->connection, &my_lease, c,
                   HTTP_METHOD_GET, "/foo", NULL,
                   c->request_body = istream_delayed_new(pool),
#ifdef HAVE_EXPECT_100
                   false,
#endif
                   &my_response_handler, c, &c->async_ref);
    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->released);
    assert(c->connection == NULL);
    assert(c->status == HTTP_STATUS_OK);
    assert(c->content_length == NULL);
    assert(c->body == NULL);
    assert(c->body_eof);
    assert(!c->body_abort);
    assert(c->request_error == NULL);
    assert(c->body_error == NULL);
}

#endif

#ifdef HAVE_EXPECT_100

/**
 * Check if the HTTP client handles "100 Continue" received without
 * announcing the expectation.
 */
static void
test_bogus_100(struct pool *pool, struct context *c)
{
    c->connection = connect_twice_100();
    client_request(pool, c->connection, &my_lease, c,
                   HTTP_METHOD_GET, "/foo", NULL, NULL, false,
                   &my_response_handler, c, &c->async_ref);

    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->released);
    assert(c->aborted);
    assert(c->request_error != NULL);
    assert(c->request_error->domain == http_client_quark());
    assert(c->request_error->code == HTTP_CLIENT_UNSPECIFIED);
    assert(strcmp(c->request_error->message, "unexpected status 100") == 0);
    g_error_free(c->request_error);
    assert(c->body_error == NULL);
}

/**
 * Check if the HTTP client handles "100 Continue" received twice
 * well.
 */
static void
test_twice_100(struct pool *pool, struct context *c)
{
    c->connection = connect_twice_100();
    client_request(pool, c->connection, &my_lease, c,
                   HTTP_METHOD_GET, "/foo", NULL,
                   c->request_body = istream_delayed_new(pool),
                   false,
                   &my_response_handler, c, &c->async_ref);
    async_ref_clear(istream_delayed_async_ref(c->request_body));

    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->released);
    assert(c->aborted);
    assert(c->request_error != NULL);
    assert(c->request_error->domain == http_client_quark());
    assert(c->request_error->code == HTTP_CLIENT_UNSPECIFIED);
    assert(strcmp(c->request_error->message, "unexpected status 100") == 0);
    g_error_free(c->request_error);
    assert(c->body_error == NULL);
}

#endif

/**
 * Receive an empty response from the server while still sending the
 * request body.
 */
static void
test_no_body_while_sending(struct pool *pool, struct context *c)
{
    struct istream *request_body = istream_block_new(pool);

    c->connection = connect_null();
    client_request(pool, c->connection, &my_lease, c,
                   HTTP_METHOD_GET, "/foo", NULL,
                   wrap_fake_request_body(pool, request_body),
#ifdef HAVE_EXPECT_100
                   false,
#endif
                   &my_response_handler, c, &c->async_ref);

    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->released);
    assert(c->status == HTTP_STATUS_NO_CONTENT);
    assert(c->body == NULL);
    assert(!c->body_eof);
    assert(!c->body_abort);
    assert(c->request_error == NULL);
    assert(c->body_error == NULL);
}

static void
test_hold(struct pool *pool, struct context *c)
{
    struct istream *request_body = istream_block_new(pool);

    c->connection = connect_hold();
    client_request(pool, c->connection, &my_lease, c,
                   HTTP_METHOD_GET, "/foo", NULL,
                   wrap_fake_request_body(pool, request_body),
#ifdef HAVE_EXPECT_100
                   false,
#endif
                   &my_response_handler, c, &c->async_ref);

    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->released);
    assert(c->status == HTTP_STATUS_OK);
    assert(c->body == NULL);
    assert(!c->body_eof);
    assert(c->body_abort);
    assert(c->request_error == NULL);
    assert(c->body_error != NULL);
    g_error_free(c->body_error);
}


/*
 * main
 *
 */

static void
run_test(struct pool *pool, void (*test)(struct pool *pool, struct context *c)) {
    struct pool *parent = pool_new_libc(pool, "parent");
    pool_set_major(parent);

    struct context c;
    memset(&c, 0, sizeof(c));
    c.pool = pool_new_linear(parent, "test", 16384);

    test(c.pool, &c);

    pool_unref(parent);
    pool_commit();
}

static void
run_all_tests(struct pool *pool)
{
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
#ifdef ENABLE_CLOSE_IGNORED_REQUEST_BODY
    run_test(pool, test_close_ignored_request_body);
    run_test(pool, test_head_close_ignored_request_body);
    run_test(pool, test_close_request_body_eor);
    run_test(pool, test_close_request_body_eor2);
#endif
#ifdef HAVE_EXPECT_100
    run_test(pool, test_bogus_100);
    run_test(pool, test_twice_100);
#endif
    run_test(pool, test_no_body_while_sending);
    run_test(pool, test_hold);
}
