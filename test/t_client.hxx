/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "HttpResponseHandler.hxx"
#include "lease.hxx"
#include "istream/istream.hxx"
#include "istream/Handler.hxx"
#include "istream/Pointer.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/BlockIstream.hxx"
#include "istream/ByteIstream.hxx"
#include "istream/FourIstream.hxx"
#include "istream/ConcatIstream.hxx"
#include "istream/DelayedIstream.hxx"
#include "istream/FailIstream.hxx"
#include "istream/HeadIstream.hxx"
#include "istream/istream_null.hxx"
#include "istream/istream_string.hxx"
#include "istream/ZeroIstream.hxx"
#include "pool/pool.hxx"
#include "event/TimerEvent.hxx"
#include "PInstance.hxx"
#include "strmap.hxx"
#include "fb_pool.hxx"
#include "util/Cast.hxx"
#include "util/Cancellable.hxx"
#include "util/Exception.hxx"

#ifdef USE_BUCKETS
#include "istream/Bucket.hxx"
#endif

#ifdef HAVE_EXPECT_100
#include "http_client.hxx"
#endif

#include "http/Method.h"
#include "util/Compiler.h"

#include <stdexcept>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

#ifndef HAVE_CHUNKED_REQUEST_BODY
static constexpr size_t HEAD_SIZE = 16384;
#endif

static PoolPtr
NewMajorPool(struct pool &parent, const char *name)
{
    auto pool = pool_new_libc(&parent, name);
    pool_set_major(pool);
    return pool;
}

template<class Connection>
struct Context final
    : PInstance, Cancellable, Lease, HttpResponseHandler, IstreamHandler {

    PoolPtr parent_pool;

    const PoolPtr pool;

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
    std::exception_ptr request_error;

    char *content_length = nullptr;
    off_t available = 0;

    DelayedIstreamControl *delayed = nullptr;

    IstreamPointer body;
    off_t body_data = 0, consumed_body_data = 0;
    bool body_eof = false, body_abort = false, body_closed = false;

    DelayedIstreamControl *request_body = nullptr;
    bool aborted_request_body = false;
    bool close_request_body_early = false, close_request_body_eof = false;
    std::exception_ptr body_error;

#ifdef USE_BUCKETS
    bool use_buckets = false;
    bool more_buckets;
    bool read_after_buckets = false, close_after_buckets = false;
    size_t total_buckets;
    off_t available_after_bucket, available_after_bucket_partial;
#endif

    TimerEvent defer_event;
    bool deferred = false;

    Context()
        :parent_pool(NewMajorPool(root_pool, "parent")),
         pool(pool_new_linear(parent_pool, "test", 16384)),
         body(nullptr),
         defer_event(event_loop, BIND_THIS_METHOD(OnDeferred)) {
    }

    ~Context() {
        free(content_length);
        parent_pool.reset();
    }

    bool WaitingForResponse() const {
        return status == http_status_t(0) && !request_error;
    }

    void WaitForResponse() {
        while (WaitingForResponse())
            event_loop.LoopOnce();
    }

    void WaitForFirstBodyByte() {
        assert(status != http_status_t(0));
        assert(!request_error);

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

        try {
            body.FillBucketList(list);
        } catch (...) {
            body_error = std::current_exception();
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

    void OnDeferred() noexcept {
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
    void Cancel() noexcept override;

    /* virtual methods from class IstreamHandler */
    size_t OnData(const void *data, size_t length) noexcept override;
    void OnEof() noexcept override;
    void OnError(std::exception_ptr ep) noexcept override;

    /* virtual methods from class Lease */
    void ReleaseLease(gcc_unused bool reuse) noexcept override {
        assert(connection != nullptr);

        delete connection;
        connection = nullptr;
        released = true;
    }

    /* virtual methods from class HttpResponseHandler */
    void OnHttpResponse(http_status_t status, StringMap &&headers,
                        UnusedIstreamPtr body) noexcept override;
    void OnHttpError(std::exception_ptr ep) noexcept override;
};

template<class Connection>
void
Context<Connection>::Cancel() noexcept
{
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
Context<Connection>::OnData(gcc_unused const void *data, size_t length) noexcept
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
Context<Connection>::OnEof() noexcept
{
    body.Clear();
    body_eof = true;

    if (close_request_body_eof && !aborted_request_body) {
        request_body->SetError(std::make_exception_ptr(std::runtime_error("close_request_body_eof")));
    }
}

template<class Connection>
void
Context<Connection>::OnError(std::exception_ptr ep) noexcept
{
    body.Clear();
    body_abort = true;

    assert(!body_error);
    body_error = ep;
}

/*
 * http_response_handler
 *
 */

template<class Connection>
void
Context<Connection>::OnHttpResponse(http_status_t _status,
                                    StringMap &&headers,
                                    UnusedIstreamPtr _body) noexcept
{
    status = _status;
    const char *_content_length = headers.Get("content-length");
    if (_content_length != nullptr)
        content_length = strdup(_content_length);
    available = _body
        ? _body.GetAvailable(false)
        : -2;

    if (close_request_body_early && !aborted_request_body) {
        request_body->SetError(std::make_exception_ptr(std::runtime_error("close_request_body_early")));
    }

    if (response_body_byte) {
        assert(_body);
        _body = istream_byte_new(*pool, std::move(_body));
    }

    if (close_response_body_early)
        _body.Clear();
    else if (_body)
        body.Set(std::move(_body), *this);

#ifdef USE_BUCKETS
    if (use_buckets) {
        if (available >= 0)
            DoBuckets();
        else {
            /* try again later */
            defer_event.Schedule(std::chrono::milliseconds(10));
            deferred = true;
        }

        return;
    }
#endif

    if (read_response_body)
        ReadBody();

    if (defer_read_response_body) {
        defer_event.Schedule(Event::Duration{});
        deferred = true;
    }

    if (close_response_body_late) {
        body_closed = true;
        body.ClearAndClose();
    }

    if (delayed != nullptr) {
        std::runtime_error error("delayed_fail");
        delayed->Set(istream_fail_new(*pool, std::make_exception_ptr(error)));
    }

    fb_pool_compress();
}

template<class Connection>
void
Context<Connection>::OnHttpError(std::exception_ptr ep) noexcept
{
    assert(!request_error);
    request_error = ep;

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
    pool_commit();

    c.event_loop.Dispatch();

    assert(c.released);
    assert(c.connection == nullptr);
    assert(c.status == HTTP_STATUS_NO_CONTENT);
    assert(c.content_length == nullptr);
    assert(!c.body.IsDefined());
    assert(!c.body_eof);
    assert(!c.body_abort);
    assert(!c.request_error);
    assert(c.body_error == nullptr);
}

template<class Connection>
static void
test_body(Context<Connection> &c)
{
    c.connection = Connection::NewMirror(*c.pool, c.event_loop);
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", StringMap(*c.pool),
                          istream_string_new(*c.pool, "foobar"),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);

    c.WaitForResponse();

    assert(c.status == HTTP_STATUS_OK);
    assert(!c.request_error);
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
                          istream_string_new(*c.pool, "foobar"),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);

    c.event_loop.Dispatch();

    assert(c.released);
    assert(c.status == HTTP_STATUS_OK);
    assert(c.content_length == nullptr);
    assert(c.available == 6);
    assert(c.body_eof);
    assert(c.body_data == 6);
    assert(!c.request_error);
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

    c.event_loop.Dispatch();

    assert(c.released);
    assert(c.status == HTTP_STATUS_OK);
    assert(c.available >= 65536);
    assert(!c.body_eof);
    assert(c.body_data > 0);
    assert(!c.request_error);
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
                          istream_string_new(*c.pool, "foobar"),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);

    c.event_loop.Dispatch();

    assert(c.released);
    assert(c.status == HTTP_STATUS_OK);
    assert(c.content_length == nullptr);
    assert(c.available == 6);
    assert(!c.body.IsDefined());
    assert(c.body_data == 0);
    assert(!c.body_eof);
    assert(!c.body_abort);
    assert(!c.request_error);
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
                          istream_string_new(*c.pool, "foobar"),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);

    c.event_loop.Dispatch();

    assert(c.released);
    assert(c.status == HTTP_STATUS_OK);
    assert(c.content_length == nullptr);
    assert(c.available == 6);
    assert(!c.body.IsDefined());
    assert(c.body_data == 0);
    assert(!c.body_eof);
    assert(c.body_abort || c.body_closed);
    assert(!c.request_error);
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
                          istream_string_new(*c.pool, "foobar"),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);

    c.WaitForResponse();

    assert(c.status == HTTP_STATUS_OK);
    assert(!c.request_error);
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

static UnusedIstreamPtr
wrap_fake_request_body(gcc_unused struct pool *pool, UnusedIstreamPtr i)
{
#ifndef HAVE_CHUNKED_REQUEST_BODY
    if (i.GetAvailable(false) < 0)
        i = istream_head_new(*pool, std::move(i), HEAD_SIZE, true);
#endif
    return i;
}

template<class Connection>
static UnusedIstreamPtr
make_delayed_request_body(Context<Connection> &c) noexcept
{
    auto delayed = istream_delayed_new(*c.pool, c.event_loop);
    delayed.second.cancel_ptr = c;
    c.request_body = &delayed.second;
    return std::move(delayed.first);
}

template<class Connection>
static void
test_close_request_body_early(Context<Connection> &c)
{
    c.connection = Connection::NewMirror(*c.pool, c.event_loop);
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", StringMap(*c.pool),
                          wrap_fake_request_body(c.pool, make_delayed_request_body(c)),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);

    const std::runtime_error error("fail_request_body_early");
    c.request_body->SetError(std::make_exception_ptr(error));

    c.event_loop.Dispatch();

    assert(c.released);
    assert(c.status == 0);
    assert(!c.body.IsDefined());
    assert(!c.body_eof);
    assert(!c.body_abort);
    assert(c.body_error == nullptr);
    assert(c.request_error != nullptr);
    assert(strstr(GetFullMessage(c.request_error).c_str(), error.what()) != nullptr);
}

template<class Connection>
static void
test_close_request_body_fail(Context<Connection> &c)
{
    auto delayed = istream_delayed_new(*c.pool, c.event_loop);
    auto request_body =
        istream_cat_new(*c.pool,
                        istream_head_new(*c.pool, istream_zero_new(*c.pool),
                                         4096, false),
                        std::move(delayed.first));

    c.delayed = &delayed.second;
    c.connection = Connection::NewMirror(*c.pool, c.event_loop);
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", StringMap(*c.pool),
                          wrap_fake_request_body(c.pool, std::move(request_body)),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);

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

    if (c.body_error != nullptr && !c.request_error) {
        c.request_error = std::exchange(c.body_error, std::exception_ptr());
    }

    assert(c.request_error != nullptr);
    assert(strstr(GetFullMessage(c.request_error).c_str(), "delayed_fail") != nullptr);
    assert(c.body_error == nullptr);
}

template<class Connection>
static void
test_data_blocking(Context<Connection> &c)
{
    auto request_body =
        istream_four_new(c.pool,
                         istream_head_new(*c.pool, istream_zero_new(*c.pool),
                                          65536, false));

    c.data_blocking = 5;
    c.connection = Connection::NewMirror(*c.pool, c.event_loop);
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", StringMap(*c.pool),
                          wrap_fake_request_body(c.pool, std::move(request_body)),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);

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
    assert(!c.request_error);
    assert(c.body_error == nullptr);

    c.body.Close();

    assert(c.released);
    assert(!c.body_eof);
    assert(!c.body_abort);
    assert(!c.request_error);
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

    constexpr size_t body_size = 256;

    c.response_body_byte = true;
    c.connection = Connection::NewMirror(*c.pool, c.event_loop);
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", std::move(request_headers),
                          istream_head_new(*c.pool, istream_zero_new(*c.pool),
                                           body_size, true),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);

    c.WaitForResponse();

    assert(c.status == HTTP_STATUS_OK);
    assert(!c.request_error);

    c.WaitForFirstBodyByte();

    /* the socket is released by now, but the body isn't finished
       yet */
#ifndef NO_EARLY_RELEASE_SOCKET
    if (!c.released) {
        /* just in case we experienced a partial read and the socket
           wasn't released yet: try again after some delay, to give
           the server process another chance to send the final byte */
        usleep(1000);
        c.event_loop.LoopOnceNonBlock();
    }

    assert(c.released);
#endif
    assert(c.content_length == nullptr);
    assert(c.available == body_size);
    assert(c.body.IsDefined());
    assert(!c.body_eof);
    assert(!c.body_abort);
    assert(c.consumed_body_data < (off_t)body_size);
    assert(c.body_error == nullptr);

    /* receive the rest of the response body from the buffer */
    c.WaitForEndOfBody();

    assert(c.released);
    assert(c.body_eof);
    assert(!c.body_abort);
    assert(c.consumed_body_data == body_size);
    assert(!c.request_error);
    assert(c.body_error == nullptr);
}

template<class Connection>
static void
test_body_fail(Context<Connection> &c)
{
    c.connection = Connection::NewMirror(*c.pool, c.event_loop);

    const std::runtime_error error("body_fail");

    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", StringMap(*c.pool),
                          wrap_fake_request_body(c.pool, istream_fail_new(*c.pool, std::make_exception_ptr(error))),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);

    c.event_loop.Dispatch();

    assert(c.released);
    assert(c.aborted || c.body_abort);

    if (c.body_error != nullptr && !c.request_error) {
        c.request_error = std::exchange(c.body_error, std::exception_ptr());
    }

    assert(c.request_error != nullptr);
    assert(strstr(GetFullMessage(c.request_error).c_str(), error.what()) != nullptr);
    assert(c.body_error == nullptr);
}

template<class Connection>
static void
test_head(Context<Connection> &c)
{
    c.connection = Connection::NewMirror(*c.pool, c.event_loop);
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_HEAD, "/foo", StringMap(*c.pool),
                          istream_string_new(*c.pool, "foobar"),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);

    c.event_loop.Dispatch();

    assert(c.released);
    assert(c.connection == nullptr);
    assert(c.status == HTTP_STATUS_OK);
    assert(c.content_length != nullptr);
    assert(strcmp(c.content_length, "6") == 0);
    assert(!c.body.IsDefined());
    assert(!c.body_eof);
    assert(!c.body_abort);
    assert(!c.request_error);
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

    c.event_loop.Dispatch();

    assert(c.released);
    assert(c.connection == nullptr);
    assert(c.status == HTTP_STATUS_OK);
    assert(!c.body.IsDefined());
    assert(!c.body_eof);
    assert(!c.body_abort);
    assert(!c.request_error);
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
    assert(!c.request_error);
    assert(c.body_error == nullptr);
}

template<class Connection>
static void
test_ignored_body(Context<Connection> &c)
{
    c.connection = Connection::NewNull(*c.pool, c.event_loop);
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", StringMap(*c.pool),
                          wrap_fake_request_body(c.pool, istream_zero_new(*c.pool)),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);

    c.event_loop.Dispatch();

    assert(c.released);
    assert(c.connection == nullptr);
    assert(c.status == HTTP_STATUS_NO_CONTENT);
    assert(c.content_length == nullptr);
    assert(!c.body.IsDefined());
    assert(!c.body_eof);
    assert(!c.body_abort);
    assert(!c.request_error);
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
    c.connection = Connection::NewNull(*c.pool, c.event_loop);
    c.close_request_body_early = true;
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", StringMap(*c.pool),
                          wrap_fake_request_body(c.pool, make_delayed_request_body(c)),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);

    c.event_loop.Dispatch();

    assert(c.released);
    assert(c.connection == nullptr);
    assert(c.status == HTTP_STATUS_NO_CONTENT);
    assert(c.content_length == nullptr);
    assert(!c.body.IsDefined());
    assert(!c.body_eof);
    assert(!c.body_abort);
    assert(!c.request_error);
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
    c.connection = Connection::NewNull(*c.pool, c.event_loop);
    c.close_request_body_early = true;
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_HEAD, "/foo", StringMap(*c.pool),
                          wrap_fake_request_body(c.pool, make_delayed_request_body(c)),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);

    c.event_loop.Dispatch();

    assert(c.released);
    assert(c.connection == nullptr);
    assert(c.status == HTTP_STATUS_NO_CONTENT);
    assert(c.content_length == nullptr);
    assert(!c.body.IsDefined());
    assert(!c.body_eof);
    assert(!c.body_abort);
    assert(!c.request_error);
    assert(c.body_error == nullptr);
}

/**
 * Close request body in the response_eof handler.
 */
template<class Connection>
static void
test_close_request_body_eor(Context<Connection> &c)
{
    c.connection = Connection::NewDummy(*c.pool, c.event_loop);
    c.close_request_body_eof = true;
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", StringMap(*c.pool),
                          wrap_fake_request_body(c.pool, make_delayed_request_body(c)),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);

    c.event_loop.Dispatch();

    assert(c.released);
    assert(c.connection == nullptr);
    assert(c.status == HTTP_STATUS_OK);
    assert(c.content_length == nullptr);
    assert(!c.body.IsDefined());
    assert(c.body_eof);
    assert(!c.body_abort);
    assert(!c.request_error);
    assert(c.body_error == nullptr);
}

/**
 * Close request body in the response_eof handler.
 */
template<class Connection>
static void
test_close_request_body_eor2(Context<Connection> &c)
{
    c.connection = Connection::NewFixed(*c.pool, c.event_loop);
    c.close_request_body_eof = true;
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", StringMap(*c.pool),
                          make_delayed_request_body(c),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);

    c.event_loop.Dispatch();

    assert(c.released);
    assert(c.connection == nullptr);
    assert(c.status == HTTP_STATUS_OK);
    assert(c.content_length == nullptr);
    assert(!c.body.IsDefined());
    assert(c.body_eof);
    assert(!c.body_abort);
    assert(!c.request_error);
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


    c.event_loop.Dispatch();

    assert(c.released);
    assert(c.aborted);
    assert(c.request_error);

    try {
        FindRetrowNested<HttpClientError>(c.request_error);
        assert(false);
    } catch (const HttpClientError &e) {
        assert(e.GetCode() == HttpClientErrorCode::UNSPECIFIED);
    }

    assert(strstr(GetFullMessage(c.request_error).c_str(), "unexpected status 100") != nullptr);
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
    auto delayed = istream_delayed_new(*c.pool, c.event_loop);
    delayed.second.cancel_ptr = nullptr;
    c.request_body = &delayed.second;
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", StringMap(*c.pool),
                          std::move(delayed.first),
                          false,
                          c, c.cancel_ptr);

    c.event_loop.Dispatch();

    assert(c.released);
    assert(c.aborted);
    assert(c.request_error);

    try {
        FindRetrowNested<HttpClientError>(c.request_error);
        assert(false);
    } catch (const HttpClientError &e) {
        assert(e.GetCode() == HttpClientErrorCode::UNSPECIFIED);
    }

    assert(strstr(GetFullMessage(c.request_error).c_str(), "unexpected status 100") != nullptr);
    assert(c.body_error == nullptr);
}

/**
 * The server sends "100 Continue" and closes the socket.
 */
template<class Connection>
static void
test_close_100(Context<Connection> &c)
{
    auto request_body = istream_delayed_new(*c.pool, c.event_loop);
    request_body.second.cancel_ptr = nullptr;

    c.connection = Connection::NewClose100(*c.pool, c.event_loop);
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_POST, "/foo", StringMap(*c.pool),
                          std::move(request_body.first), true,
                          c, c.cancel_ptr);

    c.event_loop.Dispatch();

    assert(c.released);
    assert(c.aborted);
    assert(c.request_error != nullptr);
    assert(strstr(GetFullMessage(c.request_error).c_str(), "closed the socket prematurely") != nullptr);
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
    c.connection = Connection::NewNull(*c.pool, c.event_loop);
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", StringMap(*c.pool),
                          wrap_fake_request_body(c.pool, istream_block_new(*c.pool)),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);

    c.event_loop.Dispatch();

    assert(c.released);
    assert(c.status == HTTP_STATUS_NO_CONTENT);
    assert(!c.body.IsDefined());
    assert(!c.body_eof);
    assert(!c.body_abort);
    assert(!c.request_error);
    assert(c.body_error == nullptr);
}

template<class Connection>
static void
test_hold(Context<Connection> &c)
{
    c.connection = Connection::NewHold(*c.pool, c.event_loop);
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", StringMap(*c.pool),
                          wrap_fake_request_body(c.pool, istream_block_new(*c.pool)),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);

    c.event_loop.Dispatch();

    assert(c.released);
    assert(c.status == HTTP_STATUS_OK);
    assert(!c.body.IsDefined());
    assert(!c.body_eof);
    assert(c.body_abort);
    assert(!c.request_error);
    assert(c.body_error != nullptr);
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

    c.event_loop.Dispatch();

    assert(c.released);
    assert(c.status == 0);
    assert(!c.body.IsDefined());
    assert(!c.body_eof);
    assert(!c.body_abort);
    assert(c.request_error != nullptr);
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

    c.event_loop.Dispatch();

    assert(c.released);
    assert(c.status == HTTP_STATUS_OK);
    assert(!c.body_eof);
    assert(c.body_abort);
    assert(!c.request_error);
    assert(c.body_error != nullptr);
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
                          istream_null_new(*c.pool),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);

    c.WaitForResponse();

    assert(!c.request_error);
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

    c.event_loop.Dispatch();

    assert(c.released);
    assert(c.status == HTTP_STATUS_OK);
    assert(c.content_length == nullptr);
    assert(c.available > 0);
    assert(!c.body_eof);
    assert(c.body_error != nullptr);
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

    c.event_loop.Dispatch();

    assert(c.released);
    assert(c.status == HTTP_STATUS_OK);
    assert(c.content_length == nullptr);
    assert(c.available > 0);
    assert(!c.body_eof);
    assert(c.body_error != nullptr);
}

#endif

template<class Connection>
static void
TestCancelNop(Context<Connection> &c)
{
    c.connection = Connection::NewNop(*c.pool, c.event_loop);
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_POST, "/foo", StringMap(*c.pool),
                          istream_null_new(*c.pool),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);

    c.cancel_ptr.Cancel();

    assert(c.released);
}

template<class Connection>
static void
TestCancelWithFailedSocketGet(Context<Connection> &c)
{
    c.connection = Connection::NewNop(*c.pool, c.event_loop);
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", StringMap(*c.pool),
                          nullptr,
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);

    c.connection->InjectSocketFailure();
    c.cancel_ptr.Cancel();

    assert(c.released);
}

template<class Connection>
static void
TestCancelWithFailedSocketPost(Context<Connection> &c)
{
    c.connection = Connection::NewNop(*c.pool, c.event_loop);
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_POST, "/foo", StringMap(*c.pool),
                          istream_null_new(*c.pool),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);

    c.connection->InjectSocketFailure();
    c.cancel_ptr.Cancel();

    assert(c.released);
}

template<class Connection>
static void
TestCloseWithFailedSocketGet(Context<Connection> &c)
{
    c.connection = Connection::NewHold(*c.pool, c.event_loop);
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_GET, "/foo", StringMap(*c.pool),
                          nullptr,
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);

    c.WaitForResponse();

    assert(!c.released);
    assert(c.status == HTTP_STATUS_OK);
    assert(c.body.IsDefined());

    c.connection->InjectSocketFailure();
    c.body.ClearAndClose();
    c.defer_event.Cancel();

    c.event_loop.Dispatch();

    assert(c.released);
}

template<class Connection>
static void
TestCloseWithFailedSocketPost(Context<Connection> &c)
{
    c.connection = Connection::NewHold(*c.pool, c.event_loop);
    c.connection->Request(c.pool, c,
                          HTTP_METHOD_POST, "/foo", StringMap(*c.pool),
                          istream_null_new(*c.pool),
#ifdef HAVE_EXPECT_100
                          false,
#endif
                          c, c.cancel_ptr);

    c.WaitForResponse();

    assert(!c.released);
    assert(c.status == HTTP_STATUS_OK);
    assert(c.body.IsDefined());

    c.connection->InjectSocketFailure();
    c.body.ClearAndClose();
    c.defer_event.Cancel();

    c.event_loop.Dispatch();

    assert(c.released);
}


/*
 * main
 *
 */

template<class Connection>
static void
run_test(void (*test)(Context<Connection> &c)) {
    Context<Connection> c;
    test(c);
}

#ifdef USE_BUCKETS

template<class Connection>
static void
run_bucket_test(void (*test)(Context<Connection> &c))
{
    Context<Connection> c;
    c.use_buckets = true;
    c.read_after_buckets = true;
    test(c);
}

#endif

template<class Connection>
static void
run_test_and_buckets(void (*test)(Context<Connection> &c))
{
    /* regular run */
    run_test(test);

#ifdef USE_BUCKETS
    run_bucket_test(test);
#endif
}

template<class Connection>
static void
run_all_tests()
{
    run_test(test_empty<Connection>);
    run_test_and_buckets(test_body<Connection>);
    run_test(test_read_body<Connection>);
#ifdef ENABLE_HUGE_BODY
    run_test_and_buckets(test_huge<Connection>);
#endif
    run_test(TestCancelNop<Connection>);
    run_test(test_close_response_body_early<Connection>);
    run_test(test_close_response_body_late<Connection>);
    run_test(test_close_response_body_data<Connection>);
    run_test(test_close_request_body_early<Connection>);
    run_test(test_close_request_body_fail<Connection>);
    run_test(test_data_blocking<Connection>);
    run_test(test_data_blocking2<Connection>);
    run_test(test_body_fail<Connection>);
    run_test(test_head<Connection>);
    run_test(test_head_discard<Connection>);
    run_test(test_head_discard2<Connection>);
    run_test(test_ignored_body<Connection>);
#ifdef ENABLE_CLOSE_IGNORED_REQUEST_BODY
    run_test(test_close_ignored_request_body<Connection>);
    run_test(test_head_close_ignored_request_body<Connection>);
    run_test(test_close_request_body_eor<Connection>);
    run_test(test_close_request_body_eor2<Connection>);
#endif
#ifdef HAVE_EXPECT_100
    run_test(test_bogus_100<Connection>);
    run_test(test_twice_100<Connection>);
    run_test(test_close_100<Connection>);
#endif
    run_test(test_no_body_while_sending<Connection>);
    run_test(test_hold<Connection>);
#ifdef ENABLE_PREMATURE_CLOSE_HEADERS
    run_test(test_premature_close_headers<Connection>);
#endif
#ifdef ENABLE_PREMATURE_CLOSE_BODY
    run_test_and_buckets(test_premature_close_body<Connection>);
#endif
#ifdef USE_BUCKETS
    run_test(test_buckets<Connection>);
    run_test(test_buckets_close<Connection>);
#endif
#ifdef ENABLE_PREMATURE_END
    run_test_and_buckets(test_premature_end<Connection>);
#endif
#ifdef ENABLE_EXCESS_DATA
    run_test_and_buckets(test_excess_data<Connection>);
#endif
    run_test(test_post_empty<Connection>);
    run_test_and_buckets(TestCancelWithFailedSocketGet<Connection>);
    run_test_and_buckets(TestCancelWithFailedSocketPost<Connection>);
    run_test_and_buckets(TestCloseWithFailedSocketGet<Connection>);
    run_test_and_buckets(TestCloseWithFailedSocketPost<Connection>);
}
