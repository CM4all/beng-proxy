#include "http_response.hxx"
#include "async.hxx"
#include "lease.hxx"
#include "istream/istream.hxx"
#include "istream/istream_pointer.hxx"
#include "istream/istream_block.hxx"
#include "istream/istream_byte.hxx"
#include "istream/istream_cat.hxx"
#include "istream/istream_delayed.hxx"
#include "istream/istream_fail.hxx"
#include "istream/istream_head.hxx"
#include "istream/istream_null.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream_zero.hxx"
#include "event/TimerEvent.hxx"
#include "event/Callback.hxx"
#include "strmap.hxx"
#include "fb_pool.hxx"
#include "util/Cast.hxx"

#ifdef USE_BUCKETS
#include "istream/Bucket.hxx"
#endif

#ifdef HAVE_EXPECT_100
#include "http_client.hxx"
#endif

#include <inline/compiler.h>
#include <http/method.h>

#include <glib.h>

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

struct Connection;

static Connection *
connect_mirror(void);

static Connection *
connect_null(void);

gcc_unused
static Connection *
connect_dummy(void);

static Connection *
connect_fixed(void);

static Connection *
connect_tiny(void);

#ifdef ENABLE_HUGE_BODY
static Connection *
connect_huge(void);
#endif

#ifdef HAVE_EXPECT_100
static Connection *
connect_twice_100(void);

static Connection *
connect_close_100(void);
#endif

static Connection *
connect_hold(void);

#ifdef ENABLE_PREMATURE_CLOSE_HEADERS
static Connection *
connect_premature_close_headers(void);
#endif

#ifdef ENABLE_PREMATURE_CLOSE_BODY
static Connection *
connect_premature_close_body(void);
#endif

#ifdef ENABLE_PREMATURE_END
static Connection *
connect_premature_end(void);
#endif

#ifdef ENABLE_EXCESS_DATA
static Connection *
connect_excess_data(void);
#endif

static void
connection_close(Connection *c);

static void
client_request(struct pool *pool, Connection *connection,
               Lease &lease,
               http_method_t method, const char *uri,
               struct strmap *headers, Istream *body,
#ifdef HAVE_EXPECT_100
               bool expect_100,
#endif
               const struct http_response_handler *handler,
               void *ctx,
               struct async_operation_ref *async_ref);

static struct pool *
NewMajorPool(struct pool &parent, const char *name)
{
    auto *pool = pool_new_libc(&parent, name);
    pool_set_major(pool);
    return pool;
}

struct Context final : Lease, IstreamHandler {
    struct pool *const parent_pool, *const pool;

    unsigned data_blocking = 0;

    /**
     * Call istream_read() on the response body from inside the
     * response callback.
     */
    bool read_response_body = false;

    bool close_response_body_early = false;
    bool close_response_body_late = false;
    bool close_response_body_data = false;
    bool response_body_byte = false;
    struct async_operation_ref async_ref;
    Connection *connection = nullptr;
    bool released = false, aborted = false;
    http_status_t status = http_status_t(0);
    GError *request_error = nullptr;

    char *content_length = nullptr;
    off_t available = 0;

    Istream *delayed = nullptr;

    IstreamPointer body;
    off_t body_data = 0, consumed_body_data = 0;
    bool body_eof = false, body_abort = false, body_closed = false;

    Istream *request_body = nullptr;
    bool aborted_request_body = false;
    bool close_request_body_early = false, close_request_body_eof = false;
    GError *body_error = nullptr;

#ifdef USE_BUCKETS
    bool use_buckets = false;
    bool more_buckets;
    bool read_after_buckets = false, close_after_buckets = false;
    size_t total_buckets;
    off_t available_after_bucket, available_after_bucket_partial;
#endif

    struct async_operation operation;

    TimerEvent defer_event;
    bool deferred = false;

    Context(struct pool &root_pool)
        :parent_pool(NewMajorPool(root_pool, "parent")),
         pool(pool_new_linear(parent_pool, "test", 16384)),
         body(nullptr),
         defer_event(MakeSimpleEventCallback(Context, OnDeferred), this) {
        operation.Init2<Context>();
    }

    ~Context() {
        free(content_length);
        pool_unref(parent_pool);
        pool_commit();
    }

#ifdef USE_BUCKETS
    void DoBuckets() {
        IstreamBucketList list;
        GError *error = nullptr;
        if (!body.FillBucketList(list, &error)) {
            body_error = error;
            return;
        }

        more_buckets = list.HasMore();
        total_buckets = list.GetTotalBufferSize();

        if (total_buckets > 0) {
            size_t buckets_consumed = body.ConsumeBucketList(total_buckets);
            assert(buckets_consumed == total_buckets);
            body_data += buckets_consumed;
        }

        available_after_bucket = body.GetAvailable(false);
        available_after_bucket_partial = body.GetAvailable(true);

        if (read_after_buckets)
            body.Read();

        if (close_after_buckets) {
            body_closed = true;
            body.ClearAndClose();
            close_response_body_late = false;
        }
    }
#endif

#ifndef USE_BUCKETS
    [[noreturn]]
#endif
    void OnDeferred() {
#ifdef USE_BUCKETS
        if (use_buckets) {
            available = body.GetAvailable(false);
            DoBuckets();
        } else
#endif
            assert(false);
    }

    void ReadBody() {
        assert(body.IsDefined());

#ifdef USE_BUCKETS
        if (use_buckets)
            DoBuckets();
        else
#endif
            body.Read();
    }

    void Abort();

    /* virtual methods from class IstreamHandler */
    size_t OnData(const void *data, size_t length) override;
    void OnEof() override;
    void OnError(GError *error) override;

    /* virtual methods from class Lease */
    void ReleaseLease(gcc_unused bool reuse) override {
        assert(connection != nullptr);

        connection_close(connection);
        connection = nullptr;
        released = true;
    }
};

/*
 * async_operation
 *
 */

void
Context::Abort()
{
    g_printerr("MY_ASYNC_ABORT\n");
    assert(request_body != nullptr);
    assert(!aborted_request_body);

    request_body = nullptr;
    aborted_request_body = true;
}

/*
 * istream handler
 *
 */

size_t
Context::OnData(gcc_unused const void *data, size_t length)
{
    body_data += length;

    if (close_response_body_data) {
        body_closed = true;
        body.ClearAndClose();
        return 0;
    }

    if (data_blocking) {
        --data_blocking;
        return 0;
    }

    if (deferred)
        return 0;

    consumed_body_data += length;
    return length;
}

void
Context::OnEof()
{
    body.Clear();
    body_eof = true;

    if (close_request_body_eof && !aborted_request_body) {
        GError *error = g_error_new_literal(test_quark(), 0,
                                            "close_request_body_eof");
        istream_delayed_set_abort(*request_body, error);
    }
}

void
Context::OnError(GError *error)
{
    body.Clear();
    body_abort = true;

    assert(body_error == nullptr);
    body_error = error;
}

/*
 * http_response_handler
 *
 */

static void
my_response(http_status_t status, struct strmap *headers, Istream *body,
            void *ctx)
{
    auto &c = *(Context *)ctx;

    c.status = status;
    const char *content_length =
        strmap_get_checked(headers, "content-length");
    if (content_length != nullptr)
        c.content_length = strdup(content_length);
    c.available = body != nullptr
        ? body->GetAvailable(false)
        : -2;

    if (c.close_request_body_early && !c.aborted_request_body) {
        GError *error = g_error_new_literal(test_quark(), 0,
                                            "close_request_body_early");
        istream_delayed_set_abort(*c.request_body, error);
    }

    if (c.response_body_byte) {
        assert(body != nullptr);
        body = istream_byte_new(*c.pool, *body);
    }

    if (c.close_response_body_early)
        body->CloseUnused();
    else if (body != nullptr)
        c.body.Set(*body, c);

#ifdef USE_BUCKETS
    if (c.use_buckets) {
        if (c.available >= 0)
            c.DoBuckets();
        else {
            /* try again later */
            static constexpr struct timeval tv{0, 10000};
            c.defer_event.Add(tv);
            c.deferred = true;
        }

        return;
    }
#endif

    if (c.read_response_body)
        c.ReadBody();

    if (c.close_response_body_late) {
        c.body_closed = true;
        c.body.ClearAndClose();
    }

    if (c.delayed != nullptr) {
        GError *error = g_error_new_literal(test_quark(), 0,
                                            "delayed_fail");
        istream_delayed_set(*c.delayed, *istream_fail_new(c.pool, error));
        c.delayed->Read();
    }

    fb_pool_compress();
}

static void
my_response_abort(GError *error, void *ctx)
{
    auto &c = *(Context *)ctx;

    assert(c.request_error == nullptr);
    c.request_error = error;

    c.aborted = true;
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
test_empty(Context &c)
{
    c.connection = connect_mirror();
    client_request(c.pool, c.connection, c,
                   HTTP_METHOD_GET, "/foo", nullptr, nullptr,
#ifdef HAVE_EXPECT_100
                   false,
#endif
                   &my_response_handler, &c, &c.async_ref);
    pool_unref(c.pool);
    pool_commit();

    event_dispatch();

    assert(c.released);
    assert(c.connection == nullptr);
    assert(c.status == HTTP_STATUS_NO_CONTENT);
    assert(c.content_length == nullptr);
    assert(!c.body.IsDefined());
    assert(!c.body_eof);
    assert(!c.body_abort);
    assert(c.request_error == nullptr);
    assert(c.body_error == nullptr);
}

static void
test_body(Context &c)
{
    c.connection = connect_mirror();
    client_request(c.pool, c.connection, c,
                   HTTP_METHOD_GET, "/foo", nullptr,
                   istream_string_new(c.pool, "foobar"),
#ifdef HAVE_EXPECT_100
                   false,
#endif
                   &my_response_handler, &c, &c.async_ref);
    pool_unref(c.pool);
    pool_commit();

    event_dispatch();

    if (c.body.IsDefined())
        c.ReadBody();

    event_dispatch();

    assert(c.released);
    assert(c.status == HTTP_STATUS_OK);
    assert(c.content_length == nullptr);
    assert(c.available == 6);
    assert(c.body_eof);
    assert(c.body_data == 6);
    assert(c.request_error == nullptr);
    assert(c.body_error == nullptr);
}

/**
 * Call istream_read() on the response body from inside the response
 * callback.
 */
static void
test_read_body(Context &c)
{
    c.read_response_body = true;
    c.connection = connect_mirror();
    client_request(c.pool, c.connection, c,
                   HTTP_METHOD_GET, "/foo", nullptr,
                   istream_string_new(c.pool, "foobar"),
#ifdef HAVE_EXPECT_100
                   false,
#endif
                   &my_response_handler, &c, &c.async_ref);
    pool_unref(c.pool);
    pool_commit();

    event_dispatch();

    assert(c.released);
    assert(c.status == HTTP_STATUS_OK);
    assert(c.content_length == nullptr);
    assert(c.available == 6);
    assert(c.body_eof);
    assert(c.body_data == 6);
    assert(c.request_error == nullptr);
    assert(c.body_error == nullptr);
}

#ifdef ENABLE_HUGE_BODY

/**
 * A huge response body with declared Content-Length.
 */
static void
test_huge(Context &c)
{
    c.read_response_body = true;
    c.close_response_body_data = true;
    c.connection = connect_huge();
    client_request(c.pool, c.connection, c,
                   HTTP_METHOD_GET, "/foo", nullptr,
                   nullptr,
#ifdef HAVE_EXPECT_100
                   false,
#endif
                   &my_response_handler, &c, &c.async_ref);
    pool_unref(c.pool);
    pool_commit();

    event_dispatch();

    assert(c.released);
    assert(c.status == HTTP_STATUS_OK);
    assert(c.available >= 65536);
    assert(!c.body_eof);
    assert(c.body_data > 0);
    assert(c.request_error == nullptr);
    assert(c.body_error == nullptr);
}

#endif

static void
test_close_response_body_early(Context &c)
{
    c.close_response_body_early = true;
    c.connection = connect_mirror();
    client_request(c.pool, c.connection, c,
                   HTTP_METHOD_GET, "/foo", nullptr,
                   istream_string_new(c.pool, "foobar"),
#ifdef HAVE_EXPECT_100
                   false,
#endif
                   &my_response_handler, &c, &c.async_ref);
    pool_unref(c.pool);
    pool_commit();

    event_dispatch();

    assert(c.released);
    assert(c.status == HTTP_STATUS_OK);
    assert(c.content_length == nullptr);
    assert(c.available == 6);
    assert(!c.body.IsDefined());
    assert(c.body_data == 0);
    assert(!c.body_eof);
    assert(!c.body_abort);
    assert(c.request_error == nullptr);
    assert(c.body_error == nullptr);
}

static void
test_close_response_body_late(Context &c)
{
    c.close_response_body_late = true;
    c.connection = connect_mirror();
    client_request(c.pool, c.connection, c,
                   HTTP_METHOD_GET, "/foo", nullptr,
                   istream_string_new(c.pool, "foobar"),
#ifdef HAVE_EXPECT_100
                   false,
#endif
                   &my_response_handler, &c, &c.async_ref);
    pool_unref(c.pool);
    pool_commit();

    event_dispatch();

    assert(c.released);
    assert(c.status == HTTP_STATUS_OK);
    assert(c.content_length == nullptr);
    assert(c.available == 6);
    assert(!c.body.IsDefined());
    assert(c.body_data == 0);
    assert(!c.body_eof);
    assert(c.body_abort || c.body_closed);
    assert(c.request_error == nullptr);
    assert(c.body_error == nullptr);
}

static void
test_close_response_body_data(Context &c)
{
    c.close_response_body_data = true;
    c.connection = connect_mirror();
    client_request(c.pool, c.connection, c,
                   HTTP_METHOD_GET, "/foo", nullptr,
                   istream_string_new(c.pool, "foobar"),
#ifdef HAVE_EXPECT_100
                   false,
#endif
                   &my_response_handler, &c, &c.async_ref);
    pool_unref(c.pool);
    pool_commit();

    event_dispatch();

    if (c.body.IsDefined())
        c.ReadBody();

    event_dispatch();

    assert(c.released);
    assert(c.status == HTTP_STATUS_OK);
    assert(c.content_length == nullptr);
    assert(c.available == 6);
    assert(!c.body.IsDefined());
    assert(c.body_data == 6);
    assert(!c.body_eof);
    assert(!c.body_abort);
    assert(c.body_closed);
    assert(c.request_error == nullptr);
    assert(c.body_error == nullptr);
}

static Istream *
wrap_fake_request_body(gcc_unused struct pool *pool, Istream *i)
{
#ifndef HAVE_CHUNKED_REQUEST_BODY
    if (i->GetAvailable(false) < 0)
        i = istream_head_new(pool, *i, 8192, true);
#endif
    return i;
}

static Istream *
make_delayed_request_body(Context &c)
{
    Istream *i = c.request_body = istream_delayed_new(c.pool);
    istream_delayed_async_ref(*i)->Set(c.operation);

    return i;
}

static void
test_close_request_body_early(Context &c)
{
    Istream *request_body = make_delayed_request_body(c);

    c.connection = connect_mirror();
    client_request(c.pool, c.connection, c,
                   HTTP_METHOD_GET, "/foo", nullptr,
                   wrap_fake_request_body(c.pool, request_body),
#ifdef HAVE_EXPECT_100
                   false,
#endif
                   &my_response_handler, &c, &c.async_ref);

    GError *error = g_error_new_literal(test_quark(), 0,
                                        "fail_request_body_early");
    istream_delayed_set_abort(*request_body, error);

    pool_unref(c.pool);
    pool_commit();

    event_dispatch();

    assert(c.released);
    assert(c.status == 0);
    assert(!c.body.IsDefined());
    assert(!c.body_eof);
    assert(!c.body_abort);
    assert(c.body_error == nullptr);
    assert(c.request_error == error);
    g_error_free(error);
}

static void
test_close_request_body_fail(Context &c)
{
    Istream *delayed = istream_delayed_new(c.pool);
    Istream *request_body =
        istream_cat_new(*c.pool,
                        istream_head_new(c.pool, *istream_zero_new(c.pool),
                                         4096, false),
                        delayed);

    c.delayed = delayed;
    c.connection = connect_mirror();
    client_request(c.pool, c.connection, c,
                   HTTP_METHOD_GET, "/foo", nullptr,
                   wrap_fake_request_body(c.pool, request_body),
#ifdef HAVE_EXPECT_100
                   false,
#endif
                   &my_response_handler, &c, &c.async_ref);
    pool_unref(c.pool);
    pool_commit();

    event_dispatch();

    assert(c.released);
    assert(c.status == 200);
    assert(c.content_length == nullptr);
#ifdef HAVE_CHUNKED_REQUEST_BODY
    assert(c.available == -1);
#else
    assert(c.available == 8192);
#endif
    assert(!c.body.IsDefined());
    assert(!c.body_eof);
    assert(c.body_abort);

    if (c.body_error != nullptr && c.request_error == nullptr) {
        c.request_error = c.body_error;
        c.body_error = nullptr;
    }

    assert(c.request_error != nullptr);
    assert(strstr(c.request_error->message, "delayed_fail") != 0);
    g_error_free(c.request_error);
    assert(c.body_error == nullptr);
}

static void
test_data_blocking(Context &c)
{
    Istream *request_body =
        istream_head_new(c.pool, *istream_zero_new(c.pool), 65536, false);

    c.data_blocking = 5;
    c.connection = connect_mirror();
    client_request(c.pool, c.connection, c,
                   HTTP_METHOD_GET, "/foo", nullptr,
                   wrap_fake_request_body(c.pool, request_body),
#ifdef HAVE_EXPECT_100
                   false,
#endif
                   &my_response_handler, &c, &c.async_ref);
    pool_unref(c.pool);
    pool_commit();

    while (c.data_blocking > 0) {
        if (c.body.IsDefined())
            c.ReadBody();
        event_loop(EVLOOP_ONCE|EVLOOP_NONBLOCK);
    }

    assert(!c.released);
    assert(c.status == HTTP_STATUS_OK);
    assert(c.content_length == nullptr);
#ifdef HAVE_CHUNKED_REQUEST_BODY
    assert(c.available == -1);
#else
    assert(c.available == 8192);
#endif
    assert(c.body.IsDefined());
    assert(c.body_data > 0);
    assert(!c.body_eof);
    assert(!c.body_abort);
    assert(c.request_error == nullptr);
    assert(c.body_error == nullptr);

    c.body.Close();

    assert(c.released);
    assert(!c.body_eof);
    assert(!c.body_abort);
    assert(c.request_error == nullptr);
    assert(c.body_error == nullptr);

    /* flush all remaining events */
    event_dispatch();
}

/**
 * This produces a closed socket while the HTTP client has data left
 * in the buffer.
 */
static void
test_data_blocking2(Context &c)
{
    struct strmap *request_headers = strmap_new(c.pool);
    request_headers->Add("connection", "close");

    c.response_body_byte = true;
    c.connection = connect_mirror();
    client_request(c.pool, c.connection, c,
                   HTTP_METHOD_GET, "/foo", request_headers,
                   istream_head_new(c.pool, *istream_zero_new(c.pool), 256, true),
#ifdef HAVE_EXPECT_100
                   false,
#endif
                   &my_response_handler, &c, &c.async_ref);
    pool_unref(c.pool);
    pool_commit();

    if (c.body.IsDefined())
        c.ReadBody();
    event_dispatch();

    /* the socket is released by now, but the body isn't finished
       yet */
    assert(c.released);
    assert(c.status == HTTP_STATUS_OK);
    assert(c.content_length == nullptr);
    assert(c.available == 256);
    assert(c.body.IsDefined());
    assert(!c.body_eof);
    assert(!c.body_abort);
    assert(c.consumed_body_data < 256);
    assert(c.request_error == nullptr);
    assert(c.body_error == nullptr);

    /* receive the rest of the response body from the buffer */
    while (c.body.IsDefined()) {
        c.ReadBody();
        event_loop(EVLOOP_ONCE|EVLOOP_NONBLOCK);
    }

    assert(c.released);
    assert(c.body_eof);
    assert(!c.body_abort);
    assert(c.consumed_body_data == 256);
    assert(c.request_error == nullptr);
    assert(c.body_error == nullptr);
}

static void
test_body_fail(Context &c)
{
    c.connection = connect_mirror();

    GError *error = g_error_new_literal(test_quark(), 0,
                                        "body_fail");

    client_request(c.pool, c.connection, c,
                   HTTP_METHOD_GET, "/foo", nullptr,
                   wrap_fake_request_body(c.pool, istream_fail_new(c.pool, error)),
#ifdef HAVE_EXPECT_100
                   false,
#endif
                   &my_response_handler, &c, &c.async_ref);
    pool_unref(c.pool);
    pool_commit();

    event_dispatch();

    assert(c.released);
    assert(c.aborted || c.body_abort);

    if (c.body_error != nullptr && c.request_error == nullptr) {
        c.request_error = c.body_error;
        c.body_error = nullptr;
    }

    assert(c.request_error == error);
    g_error_free(error);
    assert(c.body_error == nullptr);
}

static void
test_head(Context &c)
{
    c.connection = connect_mirror();
    client_request(c.pool, c.connection, c,
                   HTTP_METHOD_HEAD, "/foo", nullptr,
                   istream_string_new(c.pool, "foobar"),
#ifdef HAVE_EXPECT_100
                   false,
#endif
                   &my_response_handler, &c, &c.async_ref);
    pool_unref(c.pool);
    pool_commit();

    event_dispatch();

    assert(c.released);
    assert(c.connection == nullptr);
    assert(c.status == HTTP_STATUS_OK);
    assert(c.content_length != nullptr);
    assert(strcmp(c.content_length, "6") == 0);
    assert(!c.body.IsDefined());
    assert(!c.body_eof);
    assert(!c.body_abort);
    assert(c.request_error == nullptr);
    assert(c.body_error == nullptr);
}

/**
 * Send a HEAD request.  The server sends a response body, and the
 * client library is supposed to discard it.
 */
static void
test_head_discard(Context &c)
{
    c.connection = connect_fixed();
    client_request(c.pool, c.connection, c,
                   HTTP_METHOD_HEAD, "/foo", nullptr,
                   nullptr,
#ifdef HAVE_EXPECT_100
                   false,
#endif
                   &my_response_handler, &c, &c.async_ref);
    pool_unref(c.pool);
    pool_commit();

    event_dispatch();

    assert(c.released);
    assert(c.connection == nullptr);
    assert(c.status == HTTP_STATUS_OK);
    assert(!c.body.IsDefined());
    assert(!c.body_eof);
    assert(!c.body_abort);
    assert(c.request_error == nullptr);
    assert(c.body_error == nullptr);
}

/**
 * Same as test_head_discard(), but uses connect_tiny().
 */
static void
test_head_discard2(Context &c)
{
    c.connection = connect_tiny();
    client_request(c.pool, c.connection, c,
                   HTTP_METHOD_HEAD, "/foo", nullptr,
                   nullptr,
#ifdef HAVE_EXPECT_100
                   false,
#endif
                   &my_response_handler, &c, &c.async_ref);
    pool_unref(c.pool);
    pool_commit();

    event_dispatch();

    assert(c.released);
    assert(c.connection == nullptr);
    assert(c.status == HTTP_STATUS_OK);
    assert(c.content_length != nullptr);
    unsigned long content_length = strtoul(c.content_length, nullptr, 10);
    assert(content_length == 5 || content_length == 256);
    assert(!c.body.IsDefined());
    assert(!c.body_eof);
    assert(!c.body_abort);
    assert(c.request_error == nullptr);
    assert(c.body_error == nullptr);
}

static void
test_ignored_body(Context &c)
{
    c.connection = connect_null();
    client_request(c.pool, c.connection, c,
                   HTTP_METHOD_GET, "/foo", nullptr,
                   wrap_fake_request_body(c.pool, istream_zero_new(c.pool)),
#ifdef HAVE_EXPECT_100
                   false,
#endif
                   &my_response_handler, &c, &c.async_ref);
    pool_unref(c.pool);
    pool_commit();

    event_dispatch();

    assert(c.released);
    assert(c.connection == nullptr);
    assert(c.status == HTTP_STATUS_NO_CONTENT);
    assert(c.content_length == nullptr);
    assert(!c.body.IsDefined());
    assert(!c.body_eof);
    assert(!c.body_abort);
    assert(c.request_error == nullptr);
    assert(c.body_error == nullptr);
}

#ifdef ENABLE_CLOSE_IGNORED_REQUEST_BODY

/**
 * Close request body in the response handler (with response body).
 */
static void
test_close_ignored_request_body(Context &c)
{
    Istream *request_body = make_delayed_request_body(c);

    c.connection = connect_null();
    c.close_request_body_early = true;
    client_request(c.pool, c.connection, c,
                   HTTP_METHOD_GET, "/foo", nullptr,
                   wrap_fake_request_body(c.pool, request_body),
#ifdef HAVE_EXPECT_100
                   false,
#endif
                   &my_response_handler, &c, &c.async_ref);
    pool_unref(c.pool);
    pool_commit();

    event_dispatch();

    assert(c.released);
    assert(c.connection == nullptr);
    assert(c.status == HTTP_STATUS_NO_CONTENT);
    assert(c.content_length == nullptr);
    assert(!c.body.IsDefined());
    assert(!c.body_eof);
    assert(!c.body_abort);
    assert(c.request_error == nullptr);
    assert(c.body_error == nullptr);
}

/**
 * Close request body in the response handler, method HEAD (no
 * response body).
 */
static void
test_head_close_ignored_request_body(Context &c)
{
    Istream *request_body = make_delayed_request_body(c);

    c.connection = connect_null();
    c.close_request_body_early = true;
    client_request(c.pool, c.connection, c,
                   HTTP_METHOD_HEAD, "/foo", nullptr,
                   wrap_fake_request_body(c.pool, request_body),
#ifdef HAVE_EXPECT_100
                   false,
#endif
                   &my_response_handler, &c, &c.async_ref);
    pool_unref(c.pool);
    pool_commit();

    event_dispatch();

    assert(c.released);
    assert(c.connection == nullptr);
    assert(c.status == HTTP_STATUS_NO_CONTENT);
    assert(c.content_length == nullptr);
    assert(!c.body.IsDefined());
    assert(!c.body_eof);
    assert(!c.body_abort);
    assert(c.request_error == nullptr);
    assert(c.body_error == nullptr);
}

/**
 * Close request body in the response_eof handler.
 */
static void
test_close_request_body_eor(Context &c)
{
    Istream *request_body = make_delayed_request_body(c);

    c.connection = connect_dummy();
    c.close_request_body_eof = true;
    client_request(c.pool, c.connection, c,
                   HTTP_METHOD_GET, "/foo", nullptr,
                   wrap_fake_request_body(c.pool, request_body),
#ifdef HAVE_EXPECT_100
                   false,
#endif
                   &my_response_handler, &c, &c.async_ref);
    pool_unref(c.pool);
    pool_commit();

    event_dispatch();

    assert(c.released);
    assert(c.connection == nullptr);
    assert(c.status == HTTP_STATUS_OK);
    assert(c.content_length == nullptr);
    assert(!c.body.IsDefined());
    assert(c.body_eof);
    assert(!c.body_abort);
    assert(c.request_error == nullptr);
    assert(c.body_error == nullptr);
}

/**
 * Close request body in the response_eof handler.
 */
static void
test_close_request_body_eor2(Context &c)
{
    Istream *request_body = make_delayed_request_body(c);

    c.connection = connect_fixed();
    c.close_request_body_eof = true;
    client_request(c.pool, c.connection, c,
                   HTTP_METHOD_GET, "/foo", nullptr,
                   request_body,
#ifdef HAVE_EXPECT_100
                   false,
#endif
                   &my_response_handler, &c, &c.async_ref);
    pool_unref(c.pool);
    pool_commit();

    event_dispatch();

    assert(c.released);
    assert(c.connection == nullptr);
    assert(c.status == HTTP_STATUS_OK);
    assert(c.content_length == nullptr);
    assert(!c.body.IsDefined());
    assert(c.body_eof);
    assert(!c.body_abort);
    assert(c.request_error == nullptr);
    assert(c.body_error == nullptr);
}

#endif

#ifdef HAVE_EXPECT_100

/**
 * Check if the HTTP client handles "100 Continue" received without
 * announcing the expectation.
 */
static void
test_bogus_100(Context &c)
{
    c.connection = connect_twice_100();
    client_request(c.pool, c.connection, c,
                   HTTP_METHOD_GET, "/foo", nullptr, nullptr, false,
                   &my_response_handler, &c, &c.async_ref);

    pool_unref(c.pool);
    pool_commit();

    event_dispatch();

    assert(c.released);
    assert(c.aborted);
    assert(c.request_error != nullptr);
    assert(c.request_error->domain == http_client_quark());
    assert(c.request_error->code == HTTP_CLIENT_UNSPECIFIED);
    assert(strstr(c.request_error->message, "unexpected status 100") != nullptr);
    g_error_free(c.request_error);
    assert(c.body_error == nullptr);
}

/**
 * Check if the HTTP client handles "100 Continue" received twice
 * well.
 */
static void
test_twice_100(Context &c)
{
    c.connection = connect_twice_100();
    c.request_body = istream_delayed_new(c.pool);
    istream_delayed_async_ref(*c.request_body)->Clear();
    client_request(c.pool, c.connection, c,
                   HTTP_METHOD_GET, "/foo", nullptr,
                   c.request_body,
                   false,
                   &my_response_handler, &c, &c.async_ref);
    istream_delayed_async_ref(*c.request_body)->Clear();

    pool_unref(c.pool);
    pool_commit();

    event_dispatch();

    assert(c.released);
    assert(c.aborted);
    assert(c.request_error != nullptr);
    assert(c.request_error->domain == http_client_quark());
    assert(c.request_error->code == HTTP_CLIENT_UNSPECIFIED);
    assert(strstr(c.request_error->message, "unexpected status 100") != nullptr);
    g_error_free(c.request_error);
    assert(c.body_error == nullptr);
}

/**
 * The server sends "100 Continue" and closes the socket.
 */
static void
test_close_100(Context &c)
{
    Istream *request_body = istream_delayed_new(c.pool);
    istream_delayed_async_ref(*request_body)->Clear();

    c.connection = connect_close_100();
    client_request(c.pool, c.connection, c,
                   HTTP_METHOD_POST, "/foo", nullptr, request_body, true,
                   &my_response_handler, &c, &c.async_ref);

    pool_unref(c.pool);
    pool_commit();

    event_dispatch();

    assert(c.released);
    assert(c.aborted);
    assert(c.request_error != nullptr);
    assert(strstr(c.request_error->message, "closed the socket prematurely") != nullptr);
    g_error_free(c.request_error);
    assert(c.body_error == nullptr);
}

#endif

/**
 * Receive an empty response from the server while still sending the
 * request body.
 */
static void
test_no_body_while_sending(Context &c)
{
    Istream *request_body = istream_block_new(*c.pool);

    c.connection = connect_null();
    client_request(c.pool, c.connection, c,
                   HTTP_METHOD_GET, "/foo", nullptr,
                   wrap_fake_request_body(c.pool, request_body),
#ifdef HAVE_EXPECT_100
                   false,
#endif
                   &my_response_handler, &c, &c.async_ref);

    pool_unref(c.pool);
    pool_commit();

    event_dispatch();

    assert(c.released);
    assert(c.status == HTTP_STATUS_NO_CONTENT);
    assert(!c.body.IsDefined());
    assert(!c.body_eof);
    assert(!c.body_abort);
    assert(c.request_error == nullptr);
    assert(c.body_error == nullptr);
}

static void
test_hold(Context &c)
{
    Istream *request_body = istream_block_new(*c.pool);

    c.connection = connect_hold();
    client_request(c.pool, c.connection, c,
                   HTTP_METHOD_GET, "/foo", nullptr,
                   wrap_fake_request_body(c.pool, request_body),
#ifdef HAVE_EXPECT_100
                   false,
#endif
                   &my_response_handler, &c, &c.async_ref);

    pool_unref(c.pool);
    pool_commit();

    event_dispatch();

    assert(c.released);
    assert(c.status == HTTP_STATUS_OK);
    assert(!c.body.IsDefined());
    assert(!c.body_eof);
    assert(c.body_abort);
    assert(c.request_error == nullptr);
    assert(c.body_error != nullptr);
    g_error_free(c.body_error);
}

#ifdef ENABLE_PREMATURE_CLOSE_HEADERS

/**
 * The server closes the connection before it finishes sending the
 * response headers.
 */
static void
test_premature_close_headers(Context &c)
{
    c.connection = connect_premature_close_headers();
    client_request(c.pool, c.connection, c,
                   HTTP_METHOD_GET, "/foo", nullptr, nullptr,
#ifdef HAVE_EXPECT_100
                   false,
#endif
                   &my_response_handler, &c, &c.async_ref);

    pool_unref(c.pool);
    pool_commit();

    event_dispatch();

    assert(c.released);
    assert(c.status == 0);
    assert(!c.body.IsDefined());
    assert(!c.body_eof);
    assert(!c.body_abort);
    assert(c.request_error != nullptr);
    g_error_free(c.request_error);
}

#endif

#ifdef ENABLE_PREMATURE_CLOSE_BODY

/**
 * The server closes the connection before it finishes sending the
 * response body.
 */
static void
test_premature_close_body(Context &c)
{
    c.connection = connect_premature_close_body();
    client_request(c.pool, c.connection, c,
                   HTTP_METHOD_GET, "/foo", nullptr, nullptr,
#ifdef HAVE_EXPECT_100
                   false,
#endif
                   &my_response_handler, &c, &c.async_ref);

    pool_unref(c.pool);
    pool_commit();

    event_dispatch();

    assert(c.released);
    assert(c.status == HTTP_STATUS_OK);
    assert(!c.body_eof);
    assert(c.body_abort);
    assert(c.request_error == nullptr);
    assert(c.body_error != nullptr);
    g_error_free(c.body_error);
}

#endif

/**
 * POST with empty request body.
 */
static void
test_post_empty(Context &c)
{
    c.connection = connect_mirror();
    client_request(c.pool, c.connection, c,
                   HTTP_METHOD_POST, "/foo", nullptr,
                   istream_null_new(c.pool),
#ifdef HAVE_EXPECT_100
                   false,
#endif
                   &my_response_handler, &c, &c.async_ref);
    pool_unref(c.pool);
    pool_commit();

    event_dispatch();

    if (c.body.IsDefined())
        c.ReadBody();

    event_dispatch();

    assert(c.released);
    assert(c.status == HTTP_STATUS_OK ||
           c.status == HTTP_STATUS_NO_CONTENT);
    assert(c.content_length == nullptr ||
           strcmp(c.content_length, "0") == 0);
    assert(c.available == -2);
    assert(!c.body_eof);
    assert(!c.body_abort);
    assert(c.body_data == 0);
    assert(c.request_error == nullptr);
    assert(c.body_error == nullptr);
}

#ifdef USE_BUCKETS

static void
test_buckets(Context &c)
{
    c.connection = connect_fixed();
    c.use_buckets = true;
    c.read_after_buckets = true;

    client_request(c.pool, c.connection, c,
                   HTTP_METHOD_GET, "/foo", nullptr,
                   nullptr,
#ifdef HAVE_EXPECT_100
                   false,
#endif
                   &my_response_handler, &c, &c.async_ref);
    pool_unref(c.pool);
    pool_commit();

    event_dispatch();

    assert(c.released);
    assert(c.status == HTTP_STATUS_OK);
    assert(c.content_length == nullptr);
    assert(c.available > 0);
    assert(c.body_eof);
    assert(c.body_error == nullptr);
    assert(!c.more_buckets);
    assert(c.total_buckets == (size_t)c.available);
    assert(c.available_after_bucket == 0);
    assert(c.available_after_bucket_partial == 0);
}

static void
test_buckets_close(Context &c)
{
    c.connection = connect_fixed();
    c.use_buckets = true;
    c.close_after_buckets = true;

    client_request(c.pool, c.connection, c,
                   HTTP_METHOD_GET, "/foo", nullptr,
                   nullptr,
#ifdef HAVE_EXPECT_100
                   false,
#endif
                   &my_response_handler, &c, &c.async_ref);
    pool_unref(c.pool);
    pool_commit();

    event_dispatch();

    assert(c.released);
    assert(c.status == HTTP_STATUS_OK);
    assert(c.content_length == nullptr);
    assert(c.available > 0);
    assert(!c.body_eof);
    assert(c.body_error == nullptr);
    assert(!c.more_buckets);
    assert(c.total_buckets == (size_t)c.available);
    assert(c.available_after_bucket == 0);
    assert(c.available_after_bucket_partial == 0);
}

#endif

#ifdef ENABLE_PREMATURE_END

static void
test_premature_end(Context &c)
{
    c.connection = connect_premature_end();

    client_request(c.pool, c.connection, c,
                   HTTP_METHOD_GET, "/foo", nullptr,
                   nullptr,
#ifdef HAVE_EXPECT_100
                   false,
#endif
                   &my_response_handler, &c, &c.async_ref);
    pool_unref(c.pool);
    pool_commit();

    event_dispatch();

    assert(c.released);
    assert(c.status == HTTP_STATUS_OK);
    assert(c.content_length == nullptr);
    assert(c.available > 0);
    assert(!c.body_eof);
    assert(c.body_error != nullptr);
    g_error_free(c.body_error);
}

#endif

#ifdef ENABLE_EXCESS_DATA

static void
test_excess_data(Context &c)
{
    c.connection = connect_excess_data();

    client_request(c.pool, c.connection, c,
                   HTTP_METHOD_GET, "/foo", nullptr,
                   nullptr,
#ifdef HAVE_EXPECT_100
                   false,
#endif
                   &my_response_handler, &c, &c.async_ref);
    pool_unref(c.pool);
    pool_commit();

    event_dispatch();

    assert(c.released);
    assert(c.status == HTTP_STATUS_OK);
    assert(c.content_length == nullptr);
    assert(c.available > 0);
    assert(!c.body_eof);
    assert(c.body_error != nullptr);
    g_error_free(c.body_error);
}

#endif


/*
 * main
 *
 */

static void
run_test(struct pool *pool, void (*test)(Context &c)) {
    Context c(*pool);
    test(c);
}

#ifdef USE_BUCKETS

static void
run_bucket_test(struct pool *pool, void (*test)(Context &c))
{
    Context c(*pool);
    c.use_buckets = true;
    c.read_after_buckets = true;
    test(c);
}

#endif

static void
run_test_and_buckets(struct pool *pool, void (*test)(Context &c))
{
    /* regular run */
    run_test(pool, test);

#ifdef USE_BUCKETS
    run_bucket_test(pool, test);
#endif
}

static void
run_all_tests(struct pool *pool)
{
    run_test(pool, test_empty);
    run_test_and_buckets(pool, test_body);
    run_test(pool, test_read_body);
#ifdef ENABLE_HUGE_BODY
    run_test_and_buckets(pool, test_huge);
#endif
    run_test(pool, test_close_response_body_early);
    run_test(pool, test_close_response_body_late);
    run_test(pool, test_close_response_body_data);
    run_test(pool, test_close_request_body_early);
    run_test(pool, test_close_request_body_fail);
    run_test(pool, test_data_blocking);
    run_test(pool, test_data_blocking2);
    run_test(pool, test_body_fail);
    run_test(pool, test_head);
    run_test(pool, test_head_discard);
    run_test(pool, test_head_discard2);
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
    run_test(pool, test_close_100);
#endif
    run_test(pool, test_no_body_while_sending);
    run_test(pool, test_hold);
#ifdef ENABLE_PREMATURE_CLOSE_HEADERS
    run_test(pool, test_premature_close_headers);
#endif
#ifdef ENABLE_PREMATURE_CLOSE_BODY
    run_test_and_buckets(pool, test_premature_close_body);
#endif
#ifdef USE_BUCKETS
    run_test(pool, test_buckets);
    run_test(pool, test_buckets_close);
#endif
#ifdef ENABLE_PREMATURE_END
    run_test_and_buckets(pool, test_premature_end);
#endif
#ifdef ENABLE_EXCESS_DATA
    run_test_and_buckets(pool, test_excess_data);
#endif
    run_test(pool, test_post_empty);
}
