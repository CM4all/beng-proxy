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

#include "http_client.hxx"
#include "http_headers.hxx"
#include "http_response.hxx"
#include "http_upgrade.hxx"
#include "http_util.hxx"
#include "header_parser.hxx"
#include "header_writer.hxx"
#include "http_body.hxx"
#include "istream_gb.hxx"
#include "istream/Bucket.hxx"
#include "istream/Pointer.hxx"
#include "istream/istream_cat.hxx"
#include "istream/istream_optional.hxx"
#include "istream/istream_chunked.hxx"
#include "istream/istream_dechunk.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream_null.hxx"
#include "GrowingBuffer.hxx"
#include "uri/uri_verify.hxx"
#include "direct.hxx"
#include "stopwatch.hxx"
#include "strmap.hxx"
#include "fs_lease.hxx"
#include "pool.hxx"
#include "system/Error.hxx"
#include "io/Logger.hxx"
#include "util/Cancellable.hxx"
#include "util/Cast.hxx"
#include "util/CharUtil.hxx"
#include "util/StringUtil.hxx"
#include "util/StringView.hxx"
#include "util/StringFormat.hxx"
#include "util/StaticArray.hxx"
#include "util/RuntimeError.hxx"
#include "util/Exception.hxx"
#include "util/Compiler.h"

#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

bool
IsHttpClientServerFailure(std::exception_ptr ep)
{
    try {
        FindRetrowNested<HttpClientError>(ep);
        return false;
    } catch (const HttpClientError &e) {
        return e.GetCode() != HttpClientErrorCode::UNSPECIFIED;
    }
}

/**
 * With a request body of this size or larger, we send "Expect:
 * 100-continue".
 */
static constexpr off_t EXPECT_100_THRESHOLD = 1024;

static constexpr struct timeval http_client_timeout = {
    .tv_sec = 30,
    .tv_usec = 0,
};

struct HttpClient final : BufferedSocketHandler, IstreamHandler, Cancellable {
    enum class BucketResult {
        MORE,
        BLOCKING,
        DEPLETED,
        DESTROYED,
    };

    struct ResponseBodyReader final : HttpBodyReader {
        explicit ResponseBodyReader(struct pool &_pool)
            :HttpBodyReader(_pool) {}

        HttpClient &GetClient() noexcept {
            return ContainerCast(*this, &HttpClient::response_body_reader);
        }

        /* virtual methods from class Istream */

        off_t _GetAvailable(bool partial) override {
            return GetClient().GetAvailable(partial);
        }

        void _Read() override {
            GetClient().Read();
        }

        void _FillBucketList(IstreamBucketList &list) override {
            GetClient().FillBucketList(list);
        }

        size_t _ConsumeBucketList(size_t nbytes) override {
            return GetClient().ConsumeBucketList(nbytes);
        }

        int _AsFd() override {
            return GetClient().AsFD();
        }

        void _Close() noexcept override {
            GetClient().Close();
        }
    };

    struct pool &caller_pool;

    const char *const peer_name;

    Stopwatch *const stopwatch;

    /* I/O */
    FilteredSocketLease socket;

    /* request */
    struct Request {
        /**
         * An "istream_optional" which blocks sending the request body
         * until the server has confirmed "100 Continue".
         */
        Istream *body;

        IstreamPointer istream;
        char content_length_buffer[32];

        /**
         * This flag is set when the request istream has submitted
         * data.  It is used to check whether the request istream is
         * unavailable, to unschedule the socket write event.
         */
        bool got_data;

        HttpResponseHandler &handler;

        explicit Request(HttpResponseHandler &_handler)
            :istream(nullptr), handler(_handler) {}
    } request;

    /* response */
    struct Response {
        enum class State : uint8_t {
            STATUS,
            HEADERS,
            BODY,
        } state;

        /**
         * This flag is true in HEAD requests.  HEAD responses may
         * contain a Content-Length header, but no response body will
         * follow (RFC 2616 4.3).
         */
        bool no_body;

        /**
         * This flag is true if we are currently calling the HTTP
         * response handler.  During this period,
         * http_client_response_stream_read() does nothing, to prevent
         * recursion.
         */
        bool in_handler;

        /**
         * Has the server sent a HTTP/1.0 response?
         */
        bool http_1_0;

        http_status_t status;
        StringMap headers;
        Istream *body;

        explicit Response(struct pool &pool)
            :headers(pool) {}
    } response;

    ResponseBodyReader response_body_reader;

    /* connection settings */
    bool keep_alive;

    HttpClient(struct pool &_caller_pool, struct pool &_pool,
               EventLoop &event_loop,
               SocketDescriptor fd, FdType fd_type,
               Lease &lease,
               const char *_peer_name,
               const SocketFilter *filter, void *filter_ctx,
               http_method_t method, const char *uri,
               HttpHeaders &&headers,
               Istream *body, bool expect_100,
               HttpResponseHandler &handler,
               CancellablePointer &cancel_ptr);

    ~HttpClient() {
        pool_unref(&caller_pool);
    }

    struct pool &GetPool() {
        return response_body_reader.GetPool();
    }

    /**
     * @return false if the #HttpClient has been destructed
     */
    gcc_pure
    bool IsValid() const {
        return socket.IsValid();
    }

    /**
     * @return false if the #HttpClient has released the socket
     */
    gcc_pure
    bool IsConnected() const {
        return socket.IsConnected();
    }

    gcc_pure
    bool CheckDirect() const {
        assert(socket.GetType() == FdType::FD_NONE || IsConnected());
        assert(response.state == Response::State::BODY);

        return response_body_reader.CheckDirect(socket.GetType());
    }

    void ScheduleWrite() {
        assert(IsConnected());

        socket.ScheduleWrite();
    }

    /**
     * Release the socket held by this object.
     */
    void ReleaseSocket(bool reuse) {
        if (socket.HasFilter())
            /* never reuse the socket if it was filtered */
            /* TODO: move the filtering layer to the tcp_stock to
               allow reusing connections */
            reuse = false;

        socket.Release(reuse);
    }

    /**
     * Release resources held by this object: the event object, the
     * socket lease, and the pool reference.
     */
    void Release(bool reuse) {
        stopwatch_dump(stopwatch);

        if (IsConnected())
            ReleaseSocket(reuse);

        /* this reference is necessary for our destructor, which
           destructs HttpBodyReader first */
        const ScopePoolRef ref(GetPool() TRACE_ARGS);

        this->~HttpClient();
    }

    std::exception_ptr PrefixError(std::exception_ptr ep) const {
        return NestException(ep,
                             FormatRuntimeError("error on HTTP connection to '%s'",
                                                peer_name));
    }

    void AbortResponseHeaders(std::exception_ptr ep);
    void AbortResponseBody(std::exception_ptr ep);
    void AbortResponse(std::exception_ptr ep);

    void AbortResponseHeaders(HttpClientErrorCode code, const char *msg) {
        AbortResponseHeaders(std::make_exception_ptr(HttpClientError(code, msg)));
    }

    void AbortResponse(HttpClientErrorCode code, const char *msg) {
        AbortResponse(std::make_exception_ptr(HttpClientError(code, msg)));
    }

    gcc_pure
    off_t GetAvailable(bool partial) const;

    void Read();

    void FillBucketList(IstreamBucketList &list);
    size_t ConsumeBucketList(size_t nbytes);

    int AsFD();
    void Close();

    BucketResult TryWriteBuckets2();
    BucketResult TryWriteBuckets();

    /**
     * @return false if the connection is closed
     */
    bool ParseStatusLine(const char *line, size_t length);

    /**
     * @return false if the connection is closed
     */
    bool HeadersFinished();

    /**
     * @return false if the connection is closed
     */
    bool HandleLine(const char *line, size_t length);

    BufferedResult ParseHeaders(const void *data, size_t length);

    BufferedResult FeedHeaders(const void *data, size_t length);

    void ResponseBodyEOF();

    BufferedResult FeedBody(const void *data, size_t length);

    BufferedResult Feed(const void *data, size_t length);

    DirectResult TryResponseDirect(int fd, FdType fd_type);

    /* virtual methods from class BufferedSocketHandler */
    BufferedResult OnBufferedData(const void *buffer, size_t size) override;
    DirectResult OnBufferedDirect(int fd, FdType fd_type) override;
    bool OnBufferedClosed() override;
    bool OnBufferedRemaining(size_t remaining) override;
    bool OnBufferedWrite() override;
    enum write_result OnBufferedBroken() override;
    void OnBufferedError(std::exception_ptr e) override;

    /* virtual methods from class Cancellable */
    void Cancel() override;

    /* virtual methods from class IstreamHandler */
    size_t OnData(const void *data, size_t length) override;
    ssize_t OnDirect(FdType type, int fd, size_t max_length) override;
    void OnEof() noexcept override;
    void OnError(std::exception_ptr ep) noexcept override;
};

/**
 * Abort receiving the response status/headers from the HTTP server.
 */
void
HttpClient::AbortResponseHeaders(std::exception_ptr ep)
{
    assert(response.state == Response::State::STATUS ||
           response.state == Response::State::HEADERS);

    if (IsConnected())
        ReleaseSocket(false);

    if (request.istream.IsDefined())
        request.istream.Close();

    request.handler.InvokeError(PrefixError(ep));
    Release(false);
}

/**
 * Abort receiving the response status/headers from the HTTP server.
 */
void
HttpClient::AbortResponseBody(std::exception_ptr ep)
{
    assert(response.state == Response::State::BODY);
    assert(response.body != nullptr);

    if (request.istream.IsDefined())
        request.istream.Close();

    if (response_body_reader.GotEndChunk()) {
        /* avoid recursing from DechunkIstream: when DechunkIstream
           reports EOF, and that handler closes the HttpClient, which
           then destroys HttpBodyReader, which finally destroys
           DechunkIstream ... */
    } else {
        response_body_reader.InvokeError(PrefixError(ep));
    }

    Release(false);
}

/**
 * Abort receiving the response status/headers/body from the HTTP
 * server.
 */
void
HttpClient::AbortResponse(std::exception_ptr ep)
{
    assert(response.state == Response::State::STATUS ||
           response.state == Response::State::HEADERS ||
           response.state == Response::State::BODY);

    if (response.state != Response::State::BODY)
        AbortResponseHeaders(ep);
    else
        AbortResponseBody(ep);
}


/*
 * istream implementation for the response body
 *
 */

inline off_t
HttpClient::GetAvailable(bool partial) const
{
    assert(response_body_reader.IsSocketDone(socket) || !socket.HasEnded());
    assert(response.state == Response::State::BODY);

    return response_body_reader.GetAvailable(socket, partial);
}

inline void
HttpClient::Read()
{
    assert(response_body_reader.IsSocketDone(socket) ||
           /* the following condition avoids calling
              FilteredSocketLease::HasEnded() when it would
              assert-fail; this can happen if the socket has been
              disconnected while there was still pending data, but our
              handler had been blocking it; in that case,
              HttpBodyReader::SocketEOF() leaves handling this
              condition to the dechunker, which however is never
              called while the handler blocks */
           (response_body_reader.IsChunked() && !IsConnected()) ||
           !socket.HasEnded());
    assert(response.state == Response::State::BODY);
    assert(response_body_reader.HasHandler());

    if (response_body_reader.IsEOF()) {
        /* just in case EOF has been reached by ConsumeBucketList() */
        ResponseBodyEOF();
        return;
    }

    if (IsConnected())
        socket.SetDirect(CheckDirect());

    if (response.in_handler)
        /* avoid recursion; the http_response_handler caller will
           continue parsing the response if possible */
        return;

    socket.Read(response_body_reader.RequireMore());
}

inline void
HttpClient::FillBucketList(IstreamBucketList &list)
{
    assert(response_body_reader.IsSocketDone(socket) || !socket.HasEnded());
    assert(response.state == Response::State::BODY);

    response_body_reader.FillBucketList(socket, list);
}

inline size_t
HttpClient::ConsumeBucketList(size_t nbytes)
{
    assert(response_body_reader.IsSocketDone(socket) || !socket.HasEnded());
    assert(response.state == Response::State::BODY);

    return response_body_reader.ConsumeBucketList(socket, nbytes);
}

inline int
HttpClient::AsFD()
{
    assert(response_body_reader.IsSocketDone(socket) || !socket.HasEnded());
    assert(response.state == Response::State::BODY);

    if (!IsConnected() || !socket.IsEmpty() || socket.HasFilter() ||
        keep_alive ||
        /* must not be chunked */
        &response_body_reader != response.body)
        return -1;

    int fd = socket.AsFD();
    if (fd < 0)
        return -1;

    Release(false);
    return fd;
}

inline void
HttpClient::Close()
{
    assert(response.state == Response::State::BODY);

    stopwatch_event(stopwatch, "close");

    if (request.istream.IsDefined())
        request.istream.Close();

    Release(false);
}

inline HttpClient::BucketResult
HttpClient::TryWriteBuckets2()
{
    if (socket.HasFilter())
        return BucketResult::MORE;

    IstreamBucketList list;

    try {
        request.istream.FillBucketList(list);
    } catch (...) {
        request.istream.Clear();
        throw;
    }

    StaticArray<struct iovec, 64> v;
    for (const auto &bucket : list) {
        if (bucket.GetType() != IstreamBucket::Type::BUFFER)
            break;

        const auto buffer = bucket.GetBuffer();
        auto &tail = v.append();
        tail.iov_base = const_cast<void *>(buffer.data);
        tail.iov_len = buffer.size;

        if (v.full())
            break;
    }

    if (v.empty()) {
        bool has_more = list.HasMore();
        return has_more
            ? BucketResult::MORE
            : BucketResult::DEPLETED;
    }

    ssize_t nbytes = socket.WriteV(v.begin(), v.size());
    if (nbytes < 0) {
        if (gcc_likely(nbytes == WRITE_BLOCKING))
            return BucketResult::BLOCKING;

        if (nbytes == WRITE_DESTROYED)
            return BucketResult::DESTROYED;

        int _errno = errno;

        stopwatch_event(stopwatch, "error");

        throw HttpClientError(HttpClientErrorCode::IO,
                              StringFormat<64>("write error (%s)",
                                               strerror(_errno)));
    }

    size_t consumed = request.istream.ConsumeBucketList(nbytes);
    assert(consumed == (size_t)nbytes);

    return list.IsDepleted(consumed)
        ? BucketResult::DEPLETED
        : BucketResult::MORE;
}

HttpClient::BucketResult
HttpClient::TryWriteBuckets()
{
    BucketResult result;

    try {
        result = TryWriteBuckets2();
    } catch (...) {
        assert(!request.istream.IsDefined());
        stopwatch_event(stopwatch, "error");
        AbortResponse(std::current_exception());
        return BucketResult::DESTROYED;
    }

    switch (result) {
    case BucketResult::MORE:
        assert(request.istream.IsDefined());
        break;

    case BucketResult::BLOCKING:
        assert(request.istream.IsDefined());
        ScheduleWrite();
        break;

    case BucketResult::DEPLETED:
        assert(request.istream.IsDefined());
        request.istream.ClearAndClose();
        socket.ScheduleReadNoTimeout(true);
        break;

    case BucketResult::DESTROYED:
        break;
    }

    return result;
}

inline bool
HttpClient::ParseStatusLine(const char *line, size_t length)
{
    assert(response.state == Response::State::STATUS);

    const char *space;
    if (length < 10 || memcmp(line, "HTTP/", 5) != 0 ||
        (space = (const char *)memchr(line + 6, ' ', length - 6)) == nullptr) {
        stopwatch_event(stopwatch, "malformed");

        AbortResponseHeaders(HttpClientErrorCode::GARBAGE,
                             "malformed HTTP status line");
        return false;
    }

    response.http_1_0 = line[7] == '0' && line[6] == '.' && line[5] == '1';

    length = line + length - space - 1;
    line = space + 1;

    if (gcc_unlikely(length < 3 || !IsDigitASCII(line[0]) ||
                     !IsDigitASCII(line[1]) || !IsDigitASCII(line[2]))) {
        stopwatch_event(stopwatch, "malformed");

        AbortResponseHeaders(HttpClientErrorCode::GARBAGE,
                             "no HTTP status found");
        return false;
    }

    response.status = (http_status_t)(((line[0] - '0') * 10 + line[1] - '0') * 10 + line[2] - '0');
    if (gcc_unlikely(!http_status_is_valid(response.status))) {
        stopwatch_event(stopwatch, "malformed");

        AbortResponseHeaders(HttpClientErrorCode::GARBAGE,
                             StringFormat<64>("invalid HTTP status %d",
                                              response.status));
        return false;
    }

    response.state = Response::State::HEADERS;
    return true;
}

inline bool
HttpClient::HeadersFinished()
{
    stopwatch_event(stopwatch, "headers");

    auto &response_headers = response.headers;

    const char *header_connection = response_headers.Remove("connection");
    keep_alive =
        (header_connection == nullptr && !response.http_1_0) ||
        (header_connection != nullptr &&
         http_list_contains_i(header_connection, "keep-alive"));

    if (http_status_is_empty(response.status) &&
        /* "100 Continue" requires special handling here, because the
           final response following it may contain a body */
        response.status != HTTP_STATUS_CONTINUE)
        response.no_body = true;

    if (response.no_body || response.status == HTTP_STATUS_CONTINUE) {
        response.body = nullptr;
        response.state = Response::State::BODY;
        return true;
    }

    const char *transfer_encoding =
        response_headers.Remove("transfer-encoding");
    const char *content_length_string =
        response_headers.Remove("content-length");

    /* remove the other hop-by-hop response headers */
    response_headers.Remove("proxy-authenticate");

    const bool upgrade = !response.http_1_0 && header_connection != nullptr &&
        transfer_encoding == nullptr && content_length_string == nullptr &&
        http_is_upgrade(response.status, header_connection);
    if (upgrade) {
        response_headers.Add("connection", "upgrade");
        keep_alive = false;
    }

    off_t content_length;
    bool chunked;
    if (transfer_encoding == nullptr ||
        strcasecmp(transfer_encoding, "chunked") != 0) {
        /* not chunked */

        if (gcc_unlikely(content_length_string == nullptr)) {
            if (keep_alive) {
                stopwatch_event(stopwatch, "malformed");

                AbortResponseHeaders(HttpClientErrorCode::UNSPECIFIED,
                                     "no Content-Length response header");
                return false;
            }
            content_length = (off_t)-1;
        } else {
            char *endptr;
            content_length = (off_t)strtoull(content_length_string,
                                             &endptr, 10);
            if (gcc_unlikely(endptr == content_length_string || *endptr != 0 ||
                             content_length < 0)) {
                stopwatch_event(stopwatch, "malformed");

                AbortResponseHeaders(HttpClientErrorCode::UNSPECIFIED,
                                     "invalid Content-Length header in response");
                return false;
            }

            if (content_length == 0) {
                response.body = nullptr;
                response.state = Response::State::BODY;
                return true;
            }
        }

        chunked = false;
    } else {
        /* chunked */

        content_length = (off_t)-1;
        chunked = true;
    }

    response.body = &response_body_reader.Init(socket.GetEventLoop(),
                                               content_length,
                                               chunked);

    response.state = Response::State::BODY;
    socket.SetDirect(CheckDirect());
    return true;
}

inline bool
HttpClient::HandleLine(const char *line, size_t length)
{
    assert(response.state == Response::State::STATUS ||
           response.state == Response::State::HEADERS);

    if (response.state == Response::State::STATUS)
        return ParseStatusLine(line, length);
    else if (length > 0) {
        header_parse_line(caller_pool, response.headers, {line, length});
        return true;
    } else
        return HeadersFinished();
}

static void
http_client_response_finished(HttpClient *client)
{
    assert(client->response.state == HttpClient::Response::State::BODY);

    stopwatch_event(client->stopwatch, "end");

    if (!client->socket.IsEmpty()) {
        LogConcat(2, client->peer_name, "excess data after HTTP response");
        client->keep_alive = false;
    }

    if (client->request.istream.IsDefined())
        client->request.istream.Close();

    client->Release(client->keep_alive &&
                    !client->request.istream.IsDefined());
}

inline BufferedResult
HttpClient::ParseHeaders(const void *_data, size_t length)
{
    assert(response.state == Response::State::STATUS ||
           response.state == Response::State::HEADERS);
    assert(_data != nullptr);
    assert(length > 0);

    const char *const buffer = (const char *)_data;
    const char *buffer_end = buffer + length;

    /* parse line by line */
    const char *start = buffer, *end;
    while ((end = (const char *)memchr(start, '\n',
                                       buffer_end - start)) != nullptr) {
        const char *const next = end + 1;

        /* strip the line */
        end = StripRight(start, end);

        /* handle this line */
        if (!HandleLine(start, end - start))
            return BufferedResult::CLOSED;

        if (response.state != Response::State::HEADERS) {
            /* header parsing is finished */
            socket.Consumed(next - buffer);
            return BufferedResult::AGAIN_EXPECT;
        }

        start = next;
    }

    /* remove the parsed part of the buffer */
    socket.Consumed(start - buffer);
    return BufferedResult::MORE;
}

void
HttpClient::ResponseBodyEOF()
{
    assert(response.state == Response::State::BODY);
    assert(response_body_reader.IsEOF());

    /* this pointer must be cleared before forwarding the EOF event to
       our response body handler.  If we forget that, the handler
       might close the request body, leading to an assertion failure
       because http_client_request_stream_abort() calls
       http_client_abort_response_body(), not knowing that the
       response body is already finished  */
    response.body = nullptr;

    response_body_reader.InvokeEof();

    http_client_response_finished(this);
}

inline BufferedResult
HttpClient::FeedBody(const void *data, size_t length)
{
    assert(response.state == Response::State::BODY);

    size_t nbytes;

    {
        const ScopePoolRef ref(GetPool() TRACE_ARGS);
        nbytes = response_body_reader.FeedBody(data, length);
        if (nbytes == 0)
            return IsValid()
                ? BufferedResult::BLOCKING
                : BufferedResult::CLOSED;
    }

    socket.Consumed(nbytes);

    if (IsConnected() && response_body_reader.IsSocketDone(socket))
        /* we don't need the socket anymore, we've got everything we
           need in the input buffer */
        ReleaseSocket(keep_alive);

    if (response_body_reader.IsEOF()) {
        ResponseBodyEOF();
        return BufferedResult::CLOSED;
    }

    if (nbytes < length)
        return BufferedResult::PARTIAL;

    if (response_body_reader.RequireMore())
        return BufferedResult::MORE;

    return BufferedResult::OK;
}

BufferedResult
HttpClient::FeedHeaders(const void *data, size_t length)
{
    assert(response.state == Response::State::STATUS ||
           response.state == Response::State::HEADERS);

    const BufferedResult result = ParseHeaders(data, length);
    if (result != BufferedResult::AGAIN_EXPECT)
        return result;

    /* the headers are finished, we can now report the response to
       the handler */
    assert(response.state == Response::State::BODY);

    if (response.status == HTTP_STATUS_CONTINUE) {
        assert(response.body == nullptr);

        if (request.body == nullptr) {
#ifndef NDEBUG
            /* assertion workaround */
            response.state = Response::State::STATUS;
#endif
            AbortResponseHeaders(HttpClientErrorCode::UNSPECIFIED,
                                 "unexpected status 100");
            return BufferedResult::CLOSED;
        }

        /* reset state, we're now expecting the real response */
        response.state = Response::State::STATUS;

        istream_optional_resume(*request.body);
        request.body = nullptr;

        if (!IsConnected()) {
#ifndef NDEBUG
            /* assertion workaround */
            response.state = HttpClient::Response::State::STATUS;
#endif
            AbortResponseHeaders(HttpClientErrorCode::UNSPECIFIED,
                                 "Peer closed the socket prematurely after status 100");
            return BufferedResult::CLOSED;
        }

        ScheduleWrite();

        /* try again */
        return BufferedResult::AGAIN_EXPECT;
    } else if (request.body != nullptr) {
        /* the server begins sending a response - he's not interested
           in the request body, discard it now */
        istream_optional_discard(*request.body);
        request.body = nullptr;
    }

    if ((response.body == nullptr ||
         response_body_reader.IsSocketDone(socket)) &&
        IsConnected())
        /* we don't need the socket anymore, we've got everything we
           need in the input buffer */
        ReleaseSocket(keep_alive);

    const ScopePoolRef ref(GetPool() TRACE_ARGS);
    const ScopePoolRef caller_ref(caller_pool TRACE_ARGS);

    auto *body = response.body;
    if (body == nullptr && !response.no_body)
        body = istream_null_new(&caller_pool);

    response.in_handler = true;
    request.handler.InvokeResponse(response.status,
                                   std::move(response.headers),
                                   body);
    response.in_handler = false;

    if (!IsValid())
        return BufferedResult::CLOSED;

    if (response.body == nullptr) {
        http_client_response_finished(this);
        return BufferedResult::CLOSED;
    }

    if (response_body_reader.IsEOF()) {
        ResponseBodyEOF();
        return BufferedResult::CLOSED;
    }

    /* now do the response body */
    return response_body_reader.RequireMore()
        ? BufferedResult::AGAIN_EXPECT
        : BufferedResult::AGAIN_OPTIONAL;
}

inline DirectResult
HttpClient::TryResponseDirect(int fd, FdType fd_type)
{
    assert(IsConnected());
    assert(response.state == Response::State::BODY);
    assert(CheckDirect());

    ssize_t nbytes = response_body_reader.TryDirect(fd, fd_type);
    if (nbytes == ISTREAM_RESULT_BLOCKING)
        /* the destination fd blocks */
        return DirectResult::BLOCKING;

    if (nbytes == ISTREAM_RESULT_CLOSED)
        /* the stream (and the whole connection) has been closed
           during the direct() callback */
        return DirectResult::CLOSED;

    if (nbytes < 0) {
        if (errno == EAGAIN)
            /* the source fd (= ours) blocks */
            return DirectResult::EMPTY;

        return DirectResult::ERRNO;
    }

    if (nbytes == ISTREAM_RESULT_EOF) {
        if (request.istream.IsDefined())
            request.istream.Close();

        response_body_reader.SocketEOF(0);
        Release(false);
        return DirectResult::CLOSED;
   }

    if (response_body_reader.IsEOF()) {
        ResponseBodyEOF();
        return DirectResult::CLOSED;
    }

    return DirectResult::OK;
}

inline BufferedResult
HttpClient::Feed(const void *data, size_t length)
{
    switch (response.state) {
    case Response::State::STATUS:
    case Response::State::HEADERS:
        return FeedHeaders(data, length);

    case Response::State::BODY:
        assert(response.body != nullptr);

        if (IsConnected() && response_body_reader.IsSocketDone(socket))
            /* we don't need the socket anymore, we've got everything
               we need in the input buffer */
            ReleaseSocket(keep_alive);

        return FeedBody(data, length);
    }

    assert(false);
    gcc_unreachable();
}

/*
 * socket_wrapper handler
 *
 */

BufferedResult
HttpClient::OnBufferedData(const void *buffer, size_t size)
{
    return Feed(buffer, size);
}

DirectResult
HttpClient::OnBufferedDirect(int fd, FdType fd_type)
{
    return TryResponseDirect(fd, fd_type);

}

bool
HttpClient::OnBufferedClosed()
{
    stopwatch_event(stopwatch, "end");

    if (request.istream.IsDefined())
        request.istream.ClearAndClose();

    /* can't reuse the socket, it was closed by the peer */
    ReleaseSocket(false);

    return true;
}

bool
HttpClient::OnBufferedRemaining(size_t remaining)
{
    if (response.state < Response::State::BODY)
        /* this information comes too early, we can't use it */
        return true;

    if (response_body_reader.SocketEOF(remaining)) {
        /* there's data left in the buffer: continue serving the
           buffer */
        return true;
    } else {
        /* finished: close the HTTP client */
        Release(false);
        return false;
    }
}

bool
HttpClient::OnBufferedWrite()
{
    request.got_data = false;

    switch (TryWriteBuckets()) {
    case HttpClient::BucketResult::MORE:
        break;

    case HttpClient::BucketResult::BLOCKING:
    case HttpClient::BucketResult::DEPLETED:
        return true;

    case HttpClient::BucketResult::DESTROYED:
        return false;
    }

    const ScopePoolRef ref(GetPool() TRACE_ARGS);

    request.istream.Read();

    const bool result = IsValid() && IsConnected();
    if (result && request.istream.IsDefined()) {
        if (request.got_data)
            ScheduleWrite();
        else
            socket.UnscheduleWrite();
    }

    return result;
}

enum write_result
HttpClient::OnBufferedBroken()
{
    /* the server has closed the connection, probably because he's not
       interested in our request body - that's ok; now we wait for his
       response */

    keep_alive = false;

    if (request.istream.IsDefined())
        request.istream.ClearAndClose();

    socket.ScheduleReadNoTimeout(true);

    return WRITE_BROKEN;
}

void
HttpClient::OnBufferedError(std::exception_ptr ep)
{
    stopwatch_event(stopwatch, "error");
    AbortResponse(NestException(ep,
                                HttpClientError(HttpClientErrorCode::IO,
                                                "HTTP client socket error")));
}

/*
 * istream handler for the request
 *
 */

inline size_t
HttpClient::OnData(const void *data, size_t length)
{
    assert(IsConnected());

    request.got_data = true;

    ssize_t nbytes = socket.Write(data, length);
    if (gcc_likely(nbytes >= 0)) {
        ScheduleWrite();
        return (size_t)nbytes;
    }

    if (gcc_likely(nbytes == WRITE_BLOCKING || nbytes == WRITE_DESTROYED ||
                   nbytes == WRITE_BROKEN))
        return 0;

    int _errno = errno;

    stopwatch_event(stopwatch, "error");

    AbortResponse(NestException(std::make_exception_ptr(MakeErrno(_errno)),
                                HttpClientError(HttpClientErrorCode::IO,
                                                "write error")));
    return 0;
}

inline ssize_t
HttpClient::OnDirect(FdType type, int fd, size_t max_length)
{
    assert(IsConnected());

    request.got_data = true;

    ssize_t nbytes = socket.WriteFrom(fd, type, max_length);
    if (gcc_likely(nbytes > 0))
        ScheduleWrite();
    else if (nbytes == WRITE_BLOCKING)
        return ISTREAM_RESULT_BLOCKING;
    else if (nbytes == WRITE_DESTROYED || nbytes == WRITE_BROKEN)
        return ISTREAM_RESULT_CLOSED;
    else if (gcc_likely(nbytes < 0)) {
        if (gcc_likely(errno == EAGAIN)) {
            request.got_data = false;
            socket.UnscheduleWrite();
        }
    }

    return nbytes;
}

void
HttpClient::OnEof() noexcept
{
    stopwatch_event(stopwatch, "request");

    assert(request.istream.IsDefined());
    request.istream.Clear();

    socket.UnscheduleWrite();
    socket.Read(false);
}

void
HttpClient::OnError(std::exception_ptr ep) noexcept
{
    assert(response.state == Response::State::STATUS ||
           response.state == Response::State::HEADERS ||
           response.state == Response::State::BODY);

    stopwatch_event(stopwatch, "abort");

    assert(request.istream.IsDefined());
    request.istream.Clear();

    if (response.state != HttpClient::Response::State::BODY)
        AbortResponseHeaders(ep);
    else if (response.body != nullptr)
        AbortResponseBody(ep);
}

/*
 * async operation
 *
 */

inline void
HttpClient::Cancel()
{
    stopwatch_event(stopwatch, "abort");

    /* Cancellable::Cancel() can only be used before the response was
       delivered to our callback */
    assert(response.state == Response::State::STATUS ||
           response.state == Response::State::HEADERS);

    if (request.istream.IsDefined())
        request.istream.Close();

    Release(false);
}

/*
 * constructor
 *
 */

inline
HttpClient::HttpClient(struct pool &_caller_pool, struct pool &_pool,
                       EventLoop &event_loop,
                       SocketDescriptor fd, FdType fd_type,
                       Lease &lease,
                       const char *_peer_name,
                       const SocketFilter *filter, void *filter_ctx,
                       http_method_t method, const char *uri,
                       HttpHeaders &&headers,
                       Istream *body, bool expect_100,
                       HttpResponseHandler &handler,
                       CancellablePointer &cancel_ptr)
    :caller_pool(_caller_pool),
     peer_name(_peer_name),
     stopwatch(stopwatch_new(&_pool, peer_name, uri)),
     socket(event_loop, fd, fd_type, lease,
            nullptr, &http_client_timeout,
            filter, filter_ctx,
            *this),
     request(handler),
     response(_caller_pool),
     response_body_reader(_pool)
{
    response.state = HttpClient::Response::State::STATUS;
    response.no_body = http_method_is_empty(method);

    pool_ref(&caller_pool);

    cancel_ptr = *this;

    /* request line */

    const char *p = p_strcat(&GetPool(),
                             http_method_to_string(method), " ", uri,
                             " HTTP/1.1\r\n", nullptr);
    Istream *request_line_stream = istream_string_new(&GetPool(), p);

    /* headers */

    GrowingBuffer &headers2 = headers.GetBuffer();

    const bool upgrade = body != nullptr && http_is_upgrade(headers);
    if (upgrade) {
        /* forward hop-by-hop headers requesting the protocol
           upgrade */
        headers.Write("connection", "upgrade");
        headers.MoveToBuffer("upgrade");
        request.body = nullptr;
    } else if (body != nullptr) {
        off_t content_length = body->GetAvailable(false);
        if (content_length == (off_t)-1) {
            header_write(headers2, "transfer-encoding", "chunked");

            /* optimized code path: if an istream_dechunked shall get
               chunked via istream_chunk, let's just skip both to
               reduce the amount of work and I/O we have to do */
            if (!istream_dechunk_check_verbatim(*body))
                body = istream_chunked_new(GetPool(), *body);
        } else {
            snprintf(request.content_length_buffer,
                     sizeof(request.content_length_buffer),
                     "%lu", (unsigned long)content_length);
            header_write(headers2, "content-length",
                         request.content_length_buffer);
        }

        off_t available = expect_100 ? body->GetAvailable(true) : 0;
        if (available < 0 || available >= EXPECT_100_THRESHOLD) {
            /* large request body: ask the server for confirmation
               that he's really interested */
            header_write(headers2, "expect", "100-continue");
            body = request.body = istream_optional_new(GetPool(), *body);
        } else
            /* short request body: send it immediately */
            request.body = nullptr;
    } else
        request.body = nullptr;

    GrowingBuffer headers3 = headers.ToBuffer();
    headers3.Write("\r\n", 2);

    Istream *header_stream = istream_gb_new(GetPool(), std::move(headers3));

    /* request istream */

    request.istream.Set(*istream_cat_new(GetPool(),
                                         request_line_stream,
                                         header_stream,
                                         body),
                        *this,
                        istream_direct_mask_to(socket.GetType()));

    socket.ScheduleReadNoTimeout(true);

    switch (TryWriteBuckets()) {
    case HttpClient::BucketResult::MORE:
        request.istream.Read();
        break;

    case HttpClient::BucketResult::BLOCKING:
    case HttpClient::BucketResult::DEPLETED:
    case HttpClient::BucketResult::DESTROYED:
        break;
    }
}

void
http_client_request(struct pool &caller_pool, EventLoop &event_loop,
                    SocketDescriptor fd, FdType fd_type,
                    Lease &lease,
                    const char *peer_name,
                    const SocketFilter *filter, void *filter_ctx,
                    http_method_t method, const char *uri,
                    HttpHeaders &&headers,
                    Istream *body, bool expect_100,
                    HttpResponseHandler &handler,
                    CancellablePointer &cancel_ptr)
{
    assert(fd.IsDefined());
    assert(http_method_is_valid(method));

    if (!uri_path_verify_quick(uri)) {
        /* need to hold this pool reference because it is guaranteed
           that the pool stays alive while the HttpResponseHandler
           runs, even if all other pool references are removed */
        const ScopePoolRef ref(caller_pool TRACE_ARGS);

        lease.ReleaseLease(true);
        if (body != nullptr)
            body->CloseUnused();

        if (filter != nullptr)
            filter->close(filter_ctx);

        handler.InvokeError(std::make_exception_ptr(HttpClientError(HttpClientErrorCode::UNSPECIFIED,
                                                                    StringFormat<256>("malformed request URI '%s'", uri))));
        return;
    }

    struct pool *pool =
        pool_new_linear(&caller_pool, "http_client_request", 4096);

    NewFromPool<HttpClient>(*pool, caller_pool, *pool, event_loop,
                            fd, fd_type,
                            lease,
                            peer_name,
                            filter, filter_ctx,
                            method, uri,
                            std::move(headers), body, expect_100,
                            handler, cancel_ptr);
    pool_unref(pool); // response_body_reader holds the reference
}
