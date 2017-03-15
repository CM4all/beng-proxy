#include "http_response.hxx"
#include "lease.hxx"
#include "istream/istream.hxx"
#include "istream/Pointer.hxx"
#include "istream/istream_block.hxx"
#include "istream/istream_byte.hxx"
#include "istream/istream_cat.hxx"
#include "istream/istream_delayed.hxx"
#include "istream/istream_fail.hxx"
#include "istream/istream_head.hxx"
#include "istream/istream_null.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream_zero.hxx"
#include "event/Loop.hxx"
#include "event/TimerEvent.hxx"
#include "event/Duration.hxx"
#include "strmap.hxx"
#include "fb_pool.hxx"
#include "util/Cast.hxx"
#include "util/Cancellable.hxx"

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

#ifndef HAVE_CHUNKED_REQUEST_BODY
static constexpr size_t HEAD_SIZE = 16384;
#endif

static inline GQuark
test_quark(void)
{
    return g_quark_from_static_string("test");
}

static struct pool *
NewMajorPool(struct pool &parent, const char *name)
{
    auto *pool = pool_new_libc(&parent, name);
    pool_set_major(pool);
    return pool;
}

template<class Connection>
struct Context final : Cancellable, Lease, HttpResponseHandler, IstreamHandler {
    EventLoop event_loop;

    struct pool *const parent_pool, *const pool;

    unsigned data_blocking = 0;

    /**
     * Call istream_read() on the response body from inside the
     * response callback.
     */
    bool read_response_body = false;

    /**
     * Defer a call to Istream::Read().
     */
    bool defer_read_response_body = false;

    bool close_response_body_early = false;
    bool close_response_body_late = false;
    bool close_response_body_data = false;
    bool response_body_byte = false;
    CancellablePointer cancel_ptr;
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

    TimerEvent defer_event;
    bool deferred = false;

    Context(struct pool &root_pool)
        :parent_pool(NewMajorPool(root_pool, "parent")),
         pool(pool_new_linear(parent_pool, "test", 16384)),
         body(nullptr),
         defer_event(event_loop, BIND_THIS_METHOD(OnDeferred)) {
    }

    ~Context() {
        free(content_length);
        pool_unref(parent_pool);
        pool_commit();
    }

    bool WaitingForResponse() const {
        return status == http_status_t(0) && request_error == nullptr;
    }

    void WaitForResponse() {
        while (WaitingForResponse())
            event_loop.LoopOnce();
    }

    void WaitForFirstBodyByte() {
        assert(status != http_status_t(0));
        assert(request_error == nullptr);

        while (body_data == 0 && body.IsDefined()) {
            assert(!body_eof);
            assert(body_error == nullptr);

            ReadBody();
            event_loop.LoopOnceNonBlock();
        }
    }

    void WaitForEndOfBody() {
        while (body.IsDefined()) {
            ReadBody();
            event_loop.LoopOnceNonBlock();
        }
    }

    /**
     * Give the client library another chance to release the
     * socket/process.  This is a workaround for spurious unit test
     * failures with the AJP client.
     */
    void WaitReleased() {
        if (!released)
            event_loop.LoopOnceNonBlock();
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

    void OnDeferred() {
        if (defer_read_response_body) {
            deferred = false;
            body.Read();
            return;
        }

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

    /* virtual methods from class Cancellable */
    void Cancel() override;

    /* virtual methods from class IstreamHandler */
    size_t OnData(const void *data, size_t length) override;
    void OnEof() override;
    void OnError(GError *error) override;

    /* virtual methods from class Lease */
    void ReleaseLease(gcc_unused bool reuse) override {
        assert(connection != nullptr);

        delete connection;
        connection = nullptr;
        released = true;
    }

    /* virtual methods from class HttpResponseHandler */
    void OnHttpResponse(http_status_t status, StringMap &&headers,
                        Istream *body) override;
    void OnHttpError(GError *error) override;
};

template<class Connection>
void
Context<Connection>::Cancel()
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

template<class Connection>
size_t
Context<Connection>::OnData(gcc_unused const void *data, size_t length)
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

template<class Connection>
void
Context<Connection>::OnEof()
{
    body.Clear();
    body_eof = true;

    if (close_request_body_eof && !aborted_request_body) {
        GError *error = g_error_new_literal(test_quark(), 0,
                                            "close_request_body_eof");
        istream_delayed_set_abort(*request_body, error);
    }
}

template<class Connection>
void
Context<Connection>::OnError(GError *error)
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

template<class Connection>
void
Context<Connection>::OnHttpResponse(http_status_t _status,
                                    StringMap &&headers,
                                    Istream *_body)
{
    status = _status;
    const char *_content_length = headers.Get("content-length");
    if (_content_length != nullptr)
        content_length = strdup(_content_length);
    available = _body != nullptr
        ? _body->GetAvailable(false)
        : -2;

    if (close_request_body_early && !aborted_request_body) {
        GError *error = g_error_new_literal(test_quark(), 0,
                                            "close_request_body_early");
        istream_delayed_set_abort(*request_body, error);
    }

    if (response_body_byte) {
        assert(_body != nullptr);
        _body = istream_byte_new(*pool, *_body);
    }

    if (close_response_body_early)
        _body->CloseUnused();
    else if (_body != nullptr)
        body.Set(*_body, *this);

#ifdef USE_BUCKETS
    if (use_buckets) {
        if (available >= 0)
            DoBuckets();
        else {
            /* try again later */
            defer_event.Add(EventDuration<0, 10000>::value);
            deferred = true;
        }

        return;
    }
#endif

    if (read_response_body)
        ReadBody();

    if (defer_read_response_body) {
        defer_event.Add(EventDuration<0>::value);
        deferred = true;
    }

    if (close_response_body_late) {
        body_closed = true;
        body.ClearAndClose();
    }

    if (delayed != nullptr) {
        GError *error = g_error_new_literal(test_quark(), 0,
                                            "delayed_fail");
        istream_delayed_set(*delayed, *istream_fail_new(pool, error));
        delayed->Read();
    }

    fb_pool_compress();
}

template<class Connection>
void
Context<Connection>::OnHttpError(GError *error)
{
    assert(request_error == nullptr);
    request_error = error;

    aborted = true;
}

/*
 * tests
 *
 */

template<class Connection>
static void
test_empty(Context<Connection> &c)
{
    c.connection = Connection::NewMirror(*c.pool, c.event_loop);
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", StringMap(*c.pool),
                          nullptr,
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);
    pool_unref(c.pool);
    pool_commit();

    c.event_loop.Dispatch();

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

template<class Connection>
static void
test_body(Context<Connection> &c)
{
    c.connection = Connection::NewMirror(*c.pool, c.event_loop);
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", StringMap(*c.pool),
                          istream_string_new(c.pool, "foobar"),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);
    pool_unref(c.pool);
    pool_commit();

    c.WaitForResponse();

    assert(c.status == HTTP_STATUS_OK);
    assert(c.request_error == nullptr);
    assert(c.content_length == nullptr);
    assert(c.available == 6);

    c.WaitForFirstBodyByte();
    c.WaitReleased();

    assert(c.released);
    assert(c.body_eof);
    assert(c.body_data == 6);
    assert(c.body_error == nullptr);
}

/**
 * Call istream_read() on the response body from inside the response
 * callback.
 */
template<class Connection>
static void
test_read_body(Context<Connection> &c)
{
    c.read_response_body = true;
    c.connection = Connection::NewMirror(*c.pool, c.event_loop);
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", StringMap(*c.pool),
                          istream_string_new(c.pool, "foobar"),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);
    pool_unref(c.pool);
    pool_commit();

    c.event_loop.Dispatch();

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
template<class Connection>
static void
test_huge(Context<Connection> &c)
{
    c.read_response_body = true;
    c.close_response_body_data = true;
    c.connection = Connection::NewHuge(*c.pool, c.event_loop);
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", StringMap(*c.pool),
                          nullptr,
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);
    pool_unref(c.pool);
    pool_commit();

    c.event_loop.Dispatch();

    assert(c.released);
    assert(c.status == HTTP_STATUS_OK);
    assert(c.available >= 65536);
    assert(!c.body_eof);
    assert(c.body_data > 0);
    assert(c.request_error == nullptr);
    assert(c.body_error == nullptr);
}

#endif

template<class Connection>
static void
test_close_response_body_early(Context<Connection> &c)
{
    c.close_response_body_early = true;
    c.connection = Connection::NewMirror(*c.pool, c.event_loop);
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", StringMap(*c.pool),
                          istream_string_new(c.pool, "foobar"),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);
    pool_unref(c.pool);
    pool_commit();

    c.event_loop.Dispatch();

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

template<class Connection>
static void
test_close_response_body_late(Context<Connection> &c)
{
    c.close_response_body_late = true;
    c.connection = Connection::NewMirror(*c.pool, c.event_loop);
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", StringMap(*c.pool),
                          istream_string_new(c.pool, "foobar"),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);
    pool_unref(c.pool);
    pool_commit();

    c.event_loop.Dispatch();

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

template<class Connection>
static void
test_close_response_body_data(Context<Connection> &c)
{
    c.close_response_body_data = true;
    c.connection = Connection::NewMirror(*c.pool, c.event_loop);
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", StringMap(*c.pool),
                          istream_string_new(c.pool, "foobar"),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);
    pool_unref(c.pool);
    pool_commit();

    c.WaitForResponse();

    assert(c.status == HTTP_STATUS_OK);
    assert(c.request_error == nullptr);
    assert(c.content_length == nullptr);
    assert(c.available == 6);

    c.WaitForFirstBodyByte();

    assert(c.released);
    assert(!c.body.IsDefined());
    assert(c.body_data == 6);
    assert(!c.body_eof);
    assert(!c.body_abort);
    assert(c.body_closed);
    assert(c.body_error == nullptr);
}

static Istream *
wrap_fake_request_body(gcc_unused struct pool *pool, Istream *i)
{
#ifndef HAVE_CHUNKED_REQUEST_BODY
    if (i->GetAvailable(false) < 0)
        i = istream_head_new(pool, *i, HEAD_SIZE, true);
#endif
    return i;
}

template<class Connection>
static Istream *
make_delayed_request_body(Context<Connection> &c)
{
    Istream *i = c.request_body = istream_delayed_new(c.pool);
    istream_delayed_cancellable_ptr(*i) = c;

    return i;
}

template<class Connection>
static void
test_close_request_body_early(Context<Connection> &c)
{
    Istream *request_body = make_delayed_request_body(c);

    c.connection = Connection::NewMirror(*c.pool, c.event_loop);
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", StringMap(*c.pool),
                          wrap_fake_request_body(c.pool, request_body),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);

    GError *error = g_error_new_literal(test_quark(), 0,
                                        "fail_request_body_early");
    istream_delayed_set_abort(*request_body, error);

    pool_unref(c.pool);
    pool_commit();

    c.event_loop.Dispatch();

    assert(c.released);
    assert(c.status == 0);
    assert(!c.body.IsDefined());
    assert(!c.body_eof);
    assert(!c.body_abort);
    assert(c.body_error == nullptr);
    assert(c.request_error == error);
    g_error_free(error);
}

template<class Connection>
static void
test_close_request_body_fail(Context<Connection> &c)
{
    Istream *delayed = istream_delayed_new(c.pool);
    Istream *request_body =
        istream_cat_new(*c.pool,
                        istream_head_new(c.pool, *istream_zero_new(c.pool),
                                         4096, false),
                        delayed);

    c.delayed = delayed;
    c.connection = Connection::NewMirror(*c.pool, c.event_loop);
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", StringMap(*c.pool),
                          wrap_fake_request_body(c.pool, request_body),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);
    pool_unref(c.pool);
    pool_commit();

    c.event_loop.Dispatch();

    assert(c.released);
    assert(c.status == 200);
    assert(c.content_length == nullptr);
#ifdef HAVE_CHUNKED_REQUEST_BODY
    assert(c.available == -1);
#else
    assert(c.available == HEAD_SIZE);
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

template<class Connection>
static void
test_data_blocking(Context<Connection> &c)
{
    fprintf(stderr, "TEST_DATA_BLOCKING\n");
    Istream *request_body =
        istream_head_new(c.pool, *istream_zero_new(c.pool), 2*65536, false);

    c.data_blocking = 5;
    c.connection = Connection::NewMirror(*c.pool, c.event_loop);
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", StringMap(*c.pool),
                          wrap_fake_request_body(c.pool, request_body),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);
    pool_unref(c.pool);
    pool_commit();

    while (c.data_blocking > 0) {
        if (c.body.IsDefined()) {
            c.ReadBody();
            c.event_loop.LoopOnceNonBlock();
        } else
            c.event_loop.LoopOnce();
    }

    assert(!c.released);
    assert(c.status == HTTP_STATUS_OK);
    assert(c.content_length == nullptr);
#ifdef HAVE_CHUNKED_REQUEST_BODY
    assert(c.available == -1);
#else
    assert(c.available == HEAD_SIZE);
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
    c.event_loop.Dispatch();
}

/**
 * This produces a closed socket while the HTTP client has data left
 * in the buffer.
 */
template<class Connection>
static void
test_data_blocking2(Context<Connection> &c)
{
    StringMap request_headers(*c.pool);
    request_headers.Add("connection", "close");

    c.response_body_byte = true;
    c.connection = Connection::NewMirror(*c.pool, c.event_loop);
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", std::move(request_headers),
                          istream_head_new(c.pool, *istream_zero_new(c.pool), 256, true),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);
    pool_unref(c.pool);
    pool_commit();

    c.WaitForResponse();

    assert(c.status == HTTP_STATUS_OK);
    assert(c.request_error == nullptr);

    c.WaitForFirstBodyByte();

    /* the socket is released by now, but the body isn't finished
       yet */
#ifndef NO_EARLY_RELEASE_SOCKET
    assert(c.released);
#endif
    assert(c.content_length == nullptr);
    assert(c.available == 256);
    assert(c.body.IsDefined());
    assert(!c.body_eof);
    assert(!c.body_abort);
    assert(c.consumed_body_data < 256);
    assert(c.body_error == nullptr);

    /* receive the rest of the response body from the buffer */
    c.WaitForEndOfBody();

    assert(c.released);
    assert(c.body_eof);
    assert(!c.body_abort);
    assert(c.consumed_body_data == 256);
    assert(c.request_error == nullptr);
    assert(c.body_error == nullptr);
}

template<class Connection>
static void
test_body_fail(Context<Connection> &c)
{
    c.connection = Connection::NewMirror(*c.pool, c.event_loop);

    GError *error = g_error_new_literal(test_quark(), 0,
                                        "body_fail");

    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", StringMap(*c.pool),
                          wrap_fake_request_body(c.pool, istream_fail_new(c.pool, error)),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);
    pool_unref(c.pool);
    pool_commit();

    c.event_loop.Dispatch();

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

template<class Connection>
static void
test_head(Context<Connection> &c)
{
    c.connection = Connection::NewMirror(*c.pool, c.event_loop);
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_HEAD, "/foo", StringMap(*c.pool),
                          istream_string_new(c.pool, "foobar"),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);
    pool_unref(c.pool);
    pool_commit();

    c.event_loop.Dispatch();

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
template<class Connection>
static void
test_head_discard(Context<Connection> &c)
{
    c.connection = Connection::NewFixed(*c.pool, c.event_loop);
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_HEAD, "/foo", StringMap(*c.pool),
                          nullptr,
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);
    pool_unref(c.pool);
    pool_commit();

    c.event_loop.Dispatch();

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
 * Same as test_head_discard(), but uses Connection::NewTiny)(*c.pool).
 */
template<class Connection>
static void
test_head_discard2(Context<Connection> &c)
{
    c.connection = Connection::NewTiny(*c.pool, c.event_loop);
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_HEAD, "/foo", StringMap(*c.pool),
                          nullptr,
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);
    pool_unref(c.pool);
    pool_commit();

    c.event_loop.Dispatch();

    assert(c.released);
    assert(c.connection == nullptr);
    assert(c.status == HTTP_STATUS_OK);
    assert(c.content_length != nullptr);
    gcc_unused
    unsigned long content_length = strtoul(c.content_length, nullptr, 10);
    assert(content_length == 5 || content_length == 256);
    assert(!c.body.IsDefined());
    assert(!c.body_eof);
    assert(!c.body_abort);
    assert(c.request_error == nullptr);
    assert(c.body_error == nullptr);
}

template<class Connection>
static void
test_ignored_body(Context<Connection> &c)
{
    c.connection = Connection::NewNull(*c.pool, c.event_loop);
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", StringMap(*c.pool),
                          wrap_fake_request_body(c.pool, istream_zero_new(c.pool)),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);
    pool_unref(c.pool);
    pool_commit();

    c.event_loop.Dispatch();

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
template<class Connection>
static void
test_close_ignored_request_body(Context<Connection> &c)
{
    Istream *request_body = make_delayed_request_body(c);

    c.connection = Connection::NewNull(*c.pool, c.event_loop);
    c.close_request_body_early = true;
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", StringMap(*c.pool),
                          wrap_fake_request_body(c.pool, request_body),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);
    pool_unref(c.pool);
    pool_commit();

    c.event_loop.Dispatch();

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
template<class Connection>
static void
test_head_close_ignored_request_body(Context<Connection> &c)
{
    Istream *request_body = make_delayed_request_body(c);

    c.connection = Connection::NewNull(*c.pool, c.event_loop);
    c.close_request_body_early = true;
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_HEAD, "/foo", StringMap(*c.pool),
                          wrap_fake_request_body(c.pool, request_body),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);
    pool_unref(c.pool);
    pool_commit();

    c.event_loop.Dispatch();

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
template<class Connection>
static void
test_close_request_body_eor(Context<Connection> &c)
{
    Istream *request_body = make_delayed_request_body(c);

    c.connection = Connection::NewDummy(*c.pool, c.event_loop);
    c.close_request_body_eof = true;
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", StringMap(*c.pool),
                          wrap_fake_request_body(c.pool, request_body),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);
    pool_unref(c.pool);
    pool_commit();

    c.event_loop.Dispatch();

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
template<class Connection>
static void
test_close_request_body_eor2(Context<Connection> &c)
{
    Istream *request_body = make_delayed_request_body(c);

    c.connection = Connection::NewFixed(*c.pool, c.event_loop);
    c.close_request_body_eof = true;
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", StringMap(*c.pool),
                          request_body,
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);
    pool_unref(c.pool);
    pool_commit();

    c.event_loop.Dispatch();

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
template<class Connection>
static void
test_bogus_100(Context<Connection> &c)
{
    c.connection = Connection::NewTwice100(*c.pool, c.event_loop);
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", StringMap(*c.pool),
                          nullptr, false,
                          c, c.cancel_ptr);

    pool_unref(c.pool);
    pool_commit();

    c.event_loop.Dispatch();

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
template<class Connection>
static void
test_twice_100(Context<Connection> &c)
{
    c.connection = Connection::NewTwice100(*c.pool, c.event_loop);
    c.request_body = istream_delayed_new(c.pool);
    istream_delayed_cancellable_ptr(*c.request_body) = nullptr;
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", StringMap(*c.pool),
                          c.request_body,
                          false,
                          c, c.cancel_ptr);
    istream_delayed_cancellable_ptr(*c.request_body) = nullptr;

    pool_unref(c.pool);
    pool_commit();

    c.event_loop.Dispatch();

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
template<class Connection>
static void
test_close_100(Context<Connection> &c)
{
    Istream *request_body = istream_delayed_new(c.pool);
    istream_delayed_cancellable_ptr(*request_body) = nullptr;

    c.connection = Connection::NewClose100(*c.pool, c.event_loop);
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_POST, "/foo", StringMap(*c.pool),
                          request_body, true,
                          c, c.cancel_ptr);

    pool_unref(c.pool);
    pool_commit();

    c.event_loop.Dispatch();

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
template<class Connection>
static void
test_no_body_while_sending(Context<Connection> &c)
{
    Istream *request_body = istream_block_new(*c.pool);

    c.connection = Connection::NewNull(*c.pool, c.event_loop);
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", StringMap(*c.pool),
                          wrap_fake_request_body(c.pool, request_body),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);

    pool_unref(c.pool);
    pool_commit();

    c.event_loop.Dispatch();

    assert(c.released);
    assert(c.status == HTTP_STATUS_NO_CONTENT);
    assert(!c.body.IsDefined());
    assert(!c.body_eof);
    assert(!c.body_abort);
    assert(c.request_error == nullptr);
    assert(c.body_error == nullptr);
}

template<class Connection>
static void
test_hold(Context<Connection> &c)
{
    Istream *request_body = istream_block_new(*c.pool);

    c.connection = Connection::NewHold(*c.pool, c.event_loop);
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", StringMap(*c.pool),
                          wrap_fake_request_body(c.pool, request_body),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);

    pool_unref(c.pool);
    pool_commit();

    c.event_loop.Dispatch();

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
template<class Connection>
static void
test_premature_close_headers(Context<Connection> &c)
{
    c.connection = Connection::NewPrematureCloseHeaders(*c.pool, c.event_loop);
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", StringMap(*c.pool),
                          nullptr,
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);

    pool_unref(c.pool);
    pool_commit();

    c.event_loop.Dispatch();

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
template<class Connection>
static void
test_premature_close_body(Context<Connection> &c)
{
    c.connection = Connection::NewPrematureCloseBody(*c.pool, c.event_loop);
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", StringMap(*c.pool), nullptr,
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);

    pool_unref(c.pool);
    pool_commit();

    c.event_loop.Dispatch();

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
template<class Connection>
static void
test_post_empty(Context<Connection> &c)
{
    c.connection = Connection::NewMirror(*c.pool, c.event_loop);
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_POST, "/foo", StringMap(*c.pool),
                          istream_null_new(c.pool),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);
    pool_unref(c.pool);
    pool_commit();

    c.WaitForResponse();

    assert(c.request_error == nullptr);
    assert(c.status == HTTP_STATUS_OK ||
           c.status == HTTP_STATUS_NO_CONTENT);
    assert(c.content_length == nullptr ||
           strcmp(c.content_length, "0") == 0);

    c.WaitForFirstBodyByte();

    if (c.body_eof) {
        assert(c.available == 0);
    } else {
        assert(c.available == -2);
    }

    assert(c.released);
    assert(!c.body_abort);
    assert(c.body_data == 0);
    assert(c.body_error == nullptr);
}

#ifdef USE_BUCKETS

template<class Connection>
static void
test_buckets(Context<Connection> &c)
{
    c.connection = Connection::NewFixed(*c.pool, c.event_loop);
    c.use_buckets = true;
    c.read_after_buckets = true;

    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", StringMap(*c.pool),
                          nullptr,
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);
    pool_unref(c.pool);
    pool_commit();

    c.event_loop.Dispatch();

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

template<class Connection>
static void
test_buckets_close(Context<Connection> &c)
{
    c.connection = Connection::NewFixed(*c.pool, c.event_loop);
    c.use_buckets = true;
    c.close_after_buckets = true;

    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", StringMap(*c.pool),
                          nullptr,
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);
    pool_unref(c.pool);
    pool_commit();

    c.event_loop.Dispatch();

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

template<class Connection>
static void
test_premature_end(Context<Connection> &c)
{
    c.connection = Connection::NewPrematureEnd(*c.pool, c.event_loop);

    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", StringMap(*c.pool),
                          nullptr,
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);
    pool_unref(c.pool);
    pool_commit();

    c.event_loop.Dispatch();

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

template<class Connection>
static void
test_excess_data(Context<Connection> &c)
{
    c.connection = Connection::NewExcessData(*c.pool, c.event_loop);

    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", StringMap(*c.pool),
                          nullptr,
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);
    pool_unref(c.pool);
    pool_commit();

    c.event_loop.Dispatch();

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

template<class Connection>
static void
run_test(struct pool *pool, void (*test)(Context<Connection> &c)) {
    Context<Connection> c(*pool);
    test(c);
}

#ifdef USE_BUCKETS

template<class Connection>
static void
run_bucket_test(struct pool *pool, void (*test)(Context<Connection> &c))
{
    Context<Connection> c(*pool);
    c.use_buckets = true;
    c.read_after_buckets = true;
    test(c);
}

#endif

template<class Connection>
static void
run_test_and_buckets(struct pool *pool, void (*test)(Context<Connection> &c))
{
    /* regular run */
    run_test(pool, test);

#ifdef USE_BUCKETS
    run_bucket_test(pool, test);
#endif
}

template<class Connection>
static void
run_all_tests(struct pool *pool)
{
    run_test(pool, test_empty<Connection>);
    run_test_and_buckets(pool, test_body<Connection>);
    run_test(pool, test_read_body<Connection>);
#ifdef ENABLE_HUGE_BODY
    run_test_and_buckets(pool, test_huge<Connection>);
#endif
    run_test(pool, test_close_response_body_early<Connection>);
    run_test(pool, test_close_response_body_late<Connection>);
    run_test(pool, test_close_response_body_data<Connection>);
    run_test(pool, test_close_request_body_early<Connection>);
    run_test(pool, test_close_request_body_fail<Connection>);
    run_test(pool, test_data_blocking<Connection>);
    run_test(pool, test_data_blocking2<Connection>);
    run_test(pool, test_body_fail<Connection>);
    run_test(pool, test_head<Connection>);
    run_test(pool, test_head_discard<Connection>);
    run_test(pool, test_head_discard2<Connection>);
    run_test(pool, test_ignored_body<Connection>);
#ifdef ENABLE_CLOSE_IGNORED_REQUEST_BODY
    run_test(pool, test_close_ignored_request_body<Connection>);
    run_test(pool, test_head_close_ignored_request_body<Connection>);
    run_test(pool, test_close_request_body_eor<Connection>);
    run_test(pool, test_close_request_body_eor2<Connection>);
#endif
#ifdef HAVE_EXPECT_100
    run_test(pool, test_bogus_100<Connection>);
    run_test(pool, test_twice_100<Connection>);
    run_test(pool, test_close_100<Connection>);
#endif
    run_test(pool, test_no_body_while_sending<Connection>);
    run_test(pool, test_hold<Connection>);
#ifdef ENABLE_PREMATURE_CLOSE_HEADERS
    run_test(pool, test_premature_close_headers<Connection>);
#endif
#ifdef ENABLE_PREMATURE_CLOSE_BODY
    run_test_and_buckets(pool, test_premature_close_body<Connection>);
#endif
#ifdef USE_BUCKETS
    run_test(pool, test_buckets<Connection>);
    run_test(pool, test_buckets_close<Connection>);
#endif
#ifdef ENABLE_PREMATURE_END
    run_test_and_buckets(pool, test_premature_end<Connection>);
#endif
#ifdef ENABLE_EXCESS_DATA
    run_test_and_buckets(pool, test_excess_data<Connection>);
#endif
    run_test(pool, test_post_empty<Connection>);
}
