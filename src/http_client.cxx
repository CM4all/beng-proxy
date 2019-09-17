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
#include "http/Headers.hxx"
#include "http/Upgrade.hxx"
#include "http/List.hxx"
#include "HttpResponseHandler.hxx"
#include "http/HeaderParser.hxx"
#include "http/HeaderWriter.hxx"
#include "http_body.hxx"
#include "istream_gb.hxx"
#include "istream/Handler.hxx"
#include "istream/Bucket.hxx"
#include "istream/Pointer.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/ConcatIstream.hxx"
#include "istream/OptionalIstream.hxx"
#include "istream/ChunkedIstream.hxx"
#include "istream/DechunkIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream_null.hxx"
#include "GrowingBuffer.hxx"
#include "uri/Verify.hxx"
#include "direct.hxx"
#include "stopwatch.hxx"
#include "strmap.hxx"
#include "fs/Lease.hxx"
#include "AllocatorPtr.hxx"
#include "pool/Holder.hxx"
#include "system/Error.hxx"
#include "io/Logger.hxx"
#include "util/Cancellable.hxx"
#include "util/Cast.hxx"
#include "util/CharUtil.hxx"
#include "util/DestructObserver.hxx"
#include "util/StringStrip.hxx"
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

static constexpr auto http_client_timeout = std::chrono::seconds(30);

class HttpClient final : PoolHolder, BufferedSocketHandler, IstreamHandler, Cancellable, DestructAnchor {
    enum class BucketResult {
        MORE,
        BLOCKING,
        DEPLETED,
        DESTROYED,
    };

    struct ResponseBodyReader final : HttpBodyReader {
        template<typename P>
        explicit ResponseBodyReader(P &&_pool) noexcept
            :HttpBodyReader(std::forward<P>(_pool)) {}

        HttpClient &GetClient() noexcept {
            return ContainerCast(*this, &HttpClient::response_body_reader);
        }

        /* virtual methods from class Istream */

        off_t _GetAvailable(bool partial) noexcept override {
            return GetClient().GetAvailable(partial);
        }

        void _Read() noexcept override {
            GetClient().Read();
        }

        void _FillBucketList(IstreamBucketList &list) override {
            GetClient().FillBucketList(list);
        }

        size_t _ConsumeBucketList(size_t nbytes) noexcept override {
            return GetClient().ConsumeBucketList(nbytes);
        }

        int _AsFd() noexcept override {
            return GetClient().AsFD();
        }

        void _Close() noexcept override {
            GetClient().Close();
        }
    };

    struct pool &caller_pool;

    const char *const peer_name;

    const StopwatchPtr stopwatch;

    EventLoop &event_loop;

    /* I/O */
    FilteredSocketLease socket;

    /* request */
    struct Request {
        /**
         * This #OptionalIstream blocks sending the request body until
         * the server has confirmed "100 Continue".
         */
        SharedPoolPtr<OptionalIstreamControl> pending_body;

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
            END,
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

        http_status_t status;
        StringMap headers;

        /**
         * The response body pending to be submitted to the
         * #HttpResponseHandler.
         */
        UnusedIstreamPtr body;

        explicit Response(struct pool &pool)
            :headers(pool) {}
    } response;

    ResponseBodyReader response_body_reader;

    /* connection settings */
    bool keep_alive;

public:
    HttpClient(PoolPtr &&_pool, struct pool &_caller_pool,
               FilteredSocket &_socket, Lease &lease,
               const char *_peer_name,
               http_method_t method, const char *uri,
               HttpHeaders &&headers,
               UnusedIstreamPtr body, bool expect_100,
               HttpResponseHandler &handler,
               CancellablePointer &cancel_ptr);

    ~HttpClient() noexcept {
        stopwatch.Dump();

        if (!socket.IsReleased())
            ReleaseSocket(false, false);
    }

private:
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
    void ReleaseSocket(bool preserve, bool reuse) {
        socket.Release(preserve, reuse);
    }

    void Destroy() {
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

    void AbortResponse(HttpClientErrorCode code, const char *msg) {
        AbortResponse(std::make_exception_ptr(HttpClientError(code, msg)));
    }

    void ResponseFinished() noexcept;

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
     * Throws on error.
     */
    void ParseStatusLine(const char *line, size_t length);

    /**
     * Throws on error.
     */
    void HeadersFinished();

    /**
     * Throws on error.
     */
    void HandleLine(const char *line, size_t length);

    /**
     * Throws on error.
     */
    BufferedResult ParseHeaders(ConstBuffer<char> b);

    /**
     * Throws on error.
     */
    BufferedResult FeedHeaders(ConstBuffer<void> b);

    void ResponseBodyEOF();

    BufferedResult FeedBody(ConstBuffer<void> b);

    DirectResult TryResponseDirect(SocketDescriptor fd, FdType fd_type);

    /* virtual methods from class BufferedSocketHandler */
    BufferedResult OnBufferedData() override;
    DirectResult OnBufferedDirect(SocketDescriptor fd, FdType fd_type) override;
    bool OnBufferedClosed() noexcept override;
    bool OnBufferedRemaining(size_t remaining) noexcept override;
    bool OnBufferedWrite() override;
    enum write_result OnBufferedBroken() noexcept override;
    void OnBufferedError(std::exception_ptr e) noexcept override;

    /* virtual methods from class Cancellable */
    void Cancel() noexcept override;

    /* virtual methods from class IstreamHandler */
    size_t OnData(const void *data, size_t length) noexcept override;
    ssize_t OnDirect(FdType type, int fd, size_t max_length) noexcept override;
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
        ReleaseSocket(false, false);

    if (request.istream.IsDefined())
        request.istream.Close();

    request.handler.InvokeError(PrefixError(ep));
    Destroy();
}

/**
 * Abort receiving the response status/headers from the HTTP server.
 */
void
HttpClient::AbortResponseBody(std::exception_ptr ep)
{
    assert(response.state == Response::State::BODY);

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

    Destroy();
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
        response_body_reader.IsChunked())
        return -1;

    int fd = socket.AsFD();
    if (fd < 0)
        return -1;

    Destroy();
    return fd;
}

inline void
HttpClient::Close()
{
    assert(response.state == Response::State::BODY);

    stopwatch.RecordEvent("close");

    if (request.istream.IsDefined())
        request.istream.Close();

    Destroy();
}

inline HttpClient::BucketResult
HttpClient::TryWriteBuckets2()
{
    if (socket.HasFilter())
        return BucketResult::MORE;

    IstreamBucketList list;
    request.istream.FillBucketList(list);

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

        throw HttpClientError(HttpClientErrorCode::IO,
                              StringFormat<64>("write error (%s)",
                                               strerror(errno)));
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
        stopwatch.RecordEvent("send_error");
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

inline void
HttpClient::ParseStatusLine(const char *line, size_t length)
{
    assert(response.state == Response::State::STATUS);

    const char *space;
    if (length < 10 || memcmp(line, "HTTP/", 5) != 0 ||
        (space = (const char *)memchr(line + 6, ' ', length - 6)) == nullptr) {
        stopwatch.RecordEvent("malformed");

        throw HttpClientError(HttpClientErrorCode::GARBAGE,
                              "malformed HTTP status line");
    }

    length = line + length - space - 1;
    line = space + 1;

    if (gcc_unlikely(length < 3 || !IsDigitASCII(line[0]) ||
                     !IsDigitASCII(line[1]) || !IsDigitASCII(line[2]))) {
        stopwatch.RecordEvent("malformed");

        throw HttpClientError(HttpClientErrorCode::GARBAGE,
                              "no HTTP status found");
    }

    response.status = (http_status_t)(((line[0] - '0') * 10 + line[1] - '0') * 10 + line[2] - '0');
    if (gcc_unlikely(!http_status_is_valid(response.status))) {
        stopwatch.RecordEvent("malformed");

        throw HttpClientError(HttpClientErrorCode::GARBAGE,
                              StringFormat<64>("invalid HTTP status %d",
                                               response.status));
    }

    response.state = Response::State::HEADERS;
}

inline void
HttpClient::HeadersFinished()
{
    stopwatch.RecordEvent("headers");

    auto &response_headers = response.headers;

    const char *header_connection = response_headers.Remove("connection");
    keep_alive = header_connection == nullptr ||
        !http_list_contains_i(header_connection, "close");

    if (http_status_is_empty(response.status) &&
        /* "100 Continue" requires special handling here, because the
           final response following it may contain a body */
        response.status != HTTP_STATUS_CONTINUE)
        response.no_body = true;

    if (response.no_body || response.status == HTTP_STATUS_CONTINUE) {
        response.state = Response::State::END;
        return;
    }

    const char *transfer_encoding =
        response_headers.Remove("transfer-encoding");
    const char *content_length_string =
        response_headers.Remove("content-length");

    /* remove the other hop-by-hop response headers */
    response_headers.Remove("proxy-authenticate");

    const bool upgrade =
        transfer_encoding == nullptr && content_length_string == nullptr &&
        http_is_upgrade(response.status, response_headers);
    if (upgrade) {
        keep_alive = false;
    }

    off_t content_length;
    bool chunked;
    if (transfer_encoding == nullptr ||
        strcasecmp(transfer_encoding, "chunked") != 0) {
        /* not chunked */

        if (gcc_unlikely(content_length_string == nullptr)) {
            if (keep_alive) {
                stopwatch.RecordEvent("malformed");

                throw HttpClientError(HttpClientErrorCode::UNSPECIFIED,
                                      "no Content-Length response header");
            }
            content_length = (off_t)-1;
        } else {
            char *endptr;
            content_length = (off_t)strtoull(content_length_string,
                                             &endptr, 10);
            if (gcc_unlikely(endptr == content_length_string || *endptr != 0 ||
                             content_length < 0)) {
                stopwatch.RecordEvent("malformed");

                throw HttpClientError(HttpClientErrorCode::UNSPECIFIED,
                                      "invalid Content-Length header in response");
            }

            if (content_length == 0) {
                response.state = Response::State::END;
                return;
            }
        }

        chunked = false;
    } else {
        /* chunked */

        content_length = (off_t)-1;
        chunked = true;
    }

    response.body = response_body_reader.Init(event_loop,
                                              content_length,
                                              chunked);

    response.state = Response::State::BODY;
    if (!socket.IsReleased())
        socket.SetDirect(CheckDirect());
}

inline void
HttpClient::HandleLine(const char *line, size_t length)
{
    assert(response.state == Response::State::STATUS ||
           response.state == Response::State::HEADERS);

    if (response.state == Response::State::STATUS)
        ParseStatusLine(line, length);
    else if (length > 0)
        header_parse_line(caller_pool, response.headers, {line, length});
    else
        HeadersFinished();
}

void
HttpClient::ResponseFinished() noexcept
{
    assert(response.state == Response::State::END);

    stopwatch.RecordEvent("end");

    if (!socket.IsEmpty()) {
        LogConcat(2, peer_name, "excess data after HTTP response");
        keep_alive = false;
    }

    if (request.istream.IsDefined())
        request.istream.Close();
    else if (IsConnected())
        ReleaseSocket(false, keep_alive);

    Destroy();
}

inline BufferedResult
HttpClient::ParseHeaders(ConstBuffer<char> b)
{
    assert(response.state == Response::State::STATUS ||
           response.state == Response::State::HEADERS);
    assert(!b.IsNull());
    assert(!b.empty());

    const char *const buffer = b.data;
    const char *buffer_end = buffer + b.size;

    /* parse line by line */
    const char *start = buffer, *end;
    while ((end = (const char *)memchr(start, '\n',
                                       buffer_end - start)) != nullptr) {
        const char *const next = end + 1;

        /* strip the line */
        end = StripRight(start, end);

        /* handle this line */
        HandleLine(start, end - start);

        if (response.state != Response::State::HEADERS) {
            /* header parsing is finished */
            socket.DisposeConsumed(next - buffer);
            return BufferedResult::AGAIN_EXPECT;
        }

        start = next;
    }

    /* remove the parsed part of the buffer */
    socket.DisposeConsumed(start - buffer);
    return BufferedResult::MORE;
}

void
HttpClient::ResponseBodyEOF()
{
    assert(response.state == Response::State::BODY);
    assert(response_body_reader.IsEOF());

    response.state = Response::State::END;

    response_body_reader.InvokeEof();

    ResponseFinished();
}

inline BufferedResult
HttpClient::FeedBody(ConstBuffer<void> b)
{
    assert(response.state == Response::State::BODY);

    size_t nbytes;

    {
        const DestructObserver destructed(*this);
        nbytes = response_body_reader.FeedBody(b.data, b.size);
        if (nbytes == 0)
            return destructed
                ? BufferedResult::CLOSED
                : BufferedResult::BLOCKING;
    }

    socket.DisposeConsumed(nbytes);

    if (IsConnected() && response_body_reader.IsSocketDone(socket))
        /* we don't need the socket anymore, we've got everything we
           need in the input buffer */
        ReleaseSocket(true, keep_alive);

    if (response_body_reader.IsEOF()) {
        ResponseBodyEOF();
        return BufferedResult::CLOSED;
    }

    if (nbytes < b.size)
        return BufferedResult::OK;

    if (response_body_reader.RequireMore())
        return BufferedResult::MORE;

    return BufferedResult::OK;
}

BufferedResult
HttpClient::FeedHeaders(ConstBuffer<void> b)
{
    assert(response.state == Response::State::STATUS ||
           response.state == Response::State::HEADERS);

    const BufferedResult result = ParseHeaders(ConstBuffer<char>::FromVoid(b));
    if (result != BufferedResult::AGAIN_EXPECT)
        return result;

    /* the headers are finished, we can now report the response to
       the handler */
    assert(response.state == Response::State::BODY ||
           response.state == Response::State::END);

    if (response.status == HTTP_STATUS_CONTINUE) {
        assert(response.state == Response::State::END);

        if (!request.pending_body) {
#ifndef NDEBUG
            /* assertion workaround */
            response.state = Response::State::STATUS;
#endif
            throw HttpClientError(HttpClientErrorCode::UNSPECIFIED,
                                  "unexpected status 100");
        }

        /* reset state, we're now expecting the real response */
        response.state = Response::State::STATUS;

        request.pending_body->Resume();
        request.pending_body.reset();

        if (!IsConnected()) {
#ifndef NDEBUG
            /* assertion workaround */
            response.state = HttpClient::Response::State::STATUS;
#endif
            throw HttpClientError(HttpClientErrorCode::UNSPECIFIED,
                                  "Peer closed the socket prematurely after status 100");
        }

        ScheduleWrite();

        /* try again */
        return BufferedResult::AGAIN_EXPECT;
    } else if (request.pending_body) {
        /* the server begins sending a response - he's not interested
           in the request body, discard it now */
        request.pending_body->Discard();
        request.pending_body.reset();
    }

    if ((response.state == Response::State::END ||
         response_body_reader.IsSocketDone(socket)) &&
        IsConnected())
        /* we don't need the socket anymore, we've got everything we
           need in the input buffer */
        ReleaseSocket(true, keep_alive);

    const DestructObserver destructed(*this);

    if (!response.body && !response.no_body)
        response.body = istream_null_new(caller_pool);

    response.in_handler = true;
    request.handler.InvokeResponse(response.status,
                                   std::move(response.headers),
                                   std::move(response.body));
    if (destructed)
        return BufferedResult::CLOSED;

    response.in_handler = false;

    if (response.state == Response::State::END) {
        ResponseFinished();
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
HttpClient::TryResponseDirect(SocketDescriptor fd, FdType fd_type)
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
        Destroy();
        return DirectResult::CLOSED;
    }

    if (response_body_reader.IsEOF()) {
        ResponseBodyEOF();
        return DirectResult::CLOSED;
    }

    return DirectResult::OK;
}

/*
 * socket_wrapper handler
 *
 */

BufferedResult
HttpClient::OnBufferedData()
{
    switch (response.state) {
    case Response::State::STATUS:
    case Response::State::HEADERS:
        try {
            return FeedHeaders(socket.ReadBuffer());
        } catch (...) {
            AbortResponseHeaders(std::current_exception());
            return BufferedResult::CLOSED;
        }

    case Response::State::BODY:
        if (IsConnected() && response_body_reader.IsSocketDone(socket))
            /* we don't need the socket anymore, we've got everything
               we need in the input buffer */
            ReleaseSocket(true, keep_alive);

        return FeedBody(socket.ReadBuffer());

    case Response::State::END:
        break;
    }

    assert(false);
    gcc_unreachable();
}

DirectResult
HttpClient::OnBufferedDirect(SocketDescriptor fd, FdType fd_type)
{
    return TryResponseDirect(fd, fd_type);

}

bool
HttpClient::OnBufferedClosed() noexcept
{
    stopwatch.RecordEvent("end");

    if (request.istream.IsDefined())
        request.istream.ClearAndClose();

    /* close the socket, but don't release it just yet; data may be
       still in flight in a SocketFilter (e.g. SSL/TLS); we'll do that
       in OnBufferedRemaining() which gets called after the
       SocketFilter has completed */
    socket.Close();

    return true;
}

bool
HttpClient::OnBufferedRemaining(size_t remaining) noexcept
{
    if (!socket.IsReleased())
        /* by now, the SocketFilter has processed all incoming data,
           and is available in the buffer; we can release the socket
           lease, but keep the (decrypted) input buffer */
        /* note: the socket can't be reused, because it was closed by
           the peer; this method gets called only after
           OnBufferedClosed() */
        ReleaseSocket(true, false);

    if (response.state < Response::State::BODY)
        /* this information comes too early, we can't use it */
        return true;

    if (response_body_reader.SocketEOF(remaining)) {
        /* there's data left in the buffer: continue serving the
           buffer */
        return true;
    } else {
        /* finished: close the HTTP client */
        Destroy();
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

    const DestructObserver destructed(*this);

    request.istream.Read();

    const bool result = !destructed && IsConnected();
    if (result && request.istream.IsDefined()) {
        if (request.got_data)
            ScheduleWrite();
        else
            socket.UnscheduleWrite();
    }

    return result;
}

enum write_result
HttpClient::OnBufferedBroken() noexcept
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
HttpClient::OnBufferedError(std::exception_ptr ep) noexcept
{
    stopwatch.RecordEvent("recv_error");
    AbortResponse(NestException(ep,
                                HttpClientError(HttpClientErrorCode::IO,
                                                "HTTP client socket error")));
}

/*
 * istream handler for the request
 *
 */

size_t
HttpClient::OnData(const void *data, size_t length) noexcept
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

    stopwatch.RecordEvent("send_error");

    AbortResponse(NestException(std::make_exception_ptr(MakeErrno(_errno,
                                                                  "Write error")),
                                HttpClientError(HttpClientErrorCode::IO,
                                                "write error")));
    return 0;
}

ssize_t
HttpClient::OnDirect(FdType type, int fd, size_t max_length) noexcept
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
    stopwatch.RecordEvent("request_end");

    assert(request.istream.IsDefined());
    request.istream.Clear();

    socket.UnscheduleWrite();
    socket.ScheduleReadNoTimeout(response_body_reader.RequireMore());
}

void
HttpClient::OnError(std::exception_ptr ep) noexcept
{
    assert(response.state == Response::State::STATUS ||
           response.state == Response::State::HEADERS ||
           response.state == Response::State::BODY ||
           response.state == Response::State::END);

    stopwatch.RecordEvent("request_error");

    assert(request.istream.IsDefined());
    request.istream.Clear();

    switch (response.state) {
    case Response::State::STATUS:
    case Response::State::HEADERS:
        AbortResponseHeaders(ep);
        break;

    case Response::State::BODY:
        AbortResponseBody(ep);
        break;

    case Response::State::END:
        break;
    }
}

/*
 * async operation
 *
 */

inline void
HttpClient::Cancel() noexcept
{
    stopwatch.RecordEvent("cancel");

    /* Cancellable::Cancel() can only be used before the response was
       delivered to our callback */
    assert(response.state == Response::State::STATUS ||
           response.state == Response::State::HEADERS);

    if (request.istream.IsDefined())
        request.istream.Close();

    Destroy();
}

/*
 * constructor
 *
 */

inline
HttpClient::HttpClient(PoolPtr &&_pool, struct pool &_caller_pool,
                       FilteredSocket &_socket, Lease &lease,
                       const char *_peer_name,
                       http_method_t method, const char *uri,
                       HttpHeaders &&headers,
                       UnusedIstreamPtr body, bool expect_100,
                       HttpResponseHandler &handler,
                       CancellablePointer &cancel_ptr)
    :PoolHolder(std::move(_pool)), caller_pool(_caller_pool),
     peer_name(_peer_name),
     stopwatch(*pool, peer_name, uri),
     event_loop(_socket.GetEventLoop()),
     socket(_socket, lease,
            Event::Duration(-1), http_client_timeout,
            *this),
     request(handler),
     response(caller_pool),
     response_body_reader(pool)
{
    response.state = HttpClient::Response::State::STATUS;
    response.no_body = http_method_is_empty(method);

    cancel_ptr = *this;

    /* request line */

    const char *p = p_strcat(&GetPool(),
                             http_method_to_string(method), " ", uri,
                             " HTTP/1.1\r\n", nullptr);
    auto request_line_stream = istream_string_new(GetPool(), p);

    /* headers */

    GrowingBuffer &headers2 = headers.GetBuffer();

    const bool upgrade = body && http_is_upgrade(headers);
    if (upgrade) {
        /* forward hop-by-hop headers requesting the protocol
           upgrade */
        headers.Write("connection", "upgrade");
        headers.MoveToBuffer("upgrade");
    } else if (body) {
        off_t content_length = body.GetAvailable(false);
        if (content_length == (off_t)-1) {
            header_write(headers2, "transfer-encoding", "chunked");

            /* optimized code path: if an istream_dechunked shall get
               chunked via istream_chunk, let's just skip both to
               reduce the amount of work and I/O we have to do */
            if (!istream_dechunk_check_verbatim(body))
                body = istream_chunked_new(GetPool(), std::move(body));
        } else {
            snprintf(request.content_length_buffer,
                     sizeof(request.content_length_buffer),
                     "%lu", (unsigned long)content_length);
            header_write(headers2, "content-length",
                         request.content_length_buffer);
        }

        off_t available = expect_100 ? body.GetAvailable(true) : 0;
        if (available < 0 || available >= EXPECT_100_THRESHOLD) {
            /* large request body: ask the server for confirmation
               that he's really interested */
            header_write(headers2, "expect", "100-continue");

            auto optional = istream_optional_new(GetPool(), std::move(body));
            body = std::move(optional.first);
            request.pending_body = std::move(optional.second);
        } else {
            /* short request body: send it immediately */
        }
    }

    GrowingBuffer headers3 = headers.ToBuffer();
    headers3.Write("\r\n", 2);

    auto header_stream = istream_gb_new(GetPool(), std::move(headers3));

    /* request istream */

    request.istream.Set(istream_cat_new(GetPool(),
                                        std::move(request_line_stream),
                                        std::move(header_stream),
                                        std::move(body)),
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
http_client_request(struct pool &caller_pool,
                    FilteredSocket &socket, Lease &lease,
                    const char *peer_name,
                    http_method_t method, const char *uri,
                    HttpHeaders &&headers,
                    UnusedIstreamPtr body, bool expect_100,
                    HttpResponseHandler &handler,
                    CancellablePointer &cancel_ptr)
{
    assert(http_method_is_valid(method));

    if (!uri_path_verify_quick(uri)) {
        lease.ReleaseLease(true);
        body.Clear();

        handler.InvokeError(std::make_exception_ptr(HttpClientError(HttpClientErrorCode::UNSPECIFIED,
                                                                    StringFormat<256>("malformed request URI '%s'", uri))));
        return;
    }

    NewFromPool<HttpClient>(pool_new_linear(&caller_pool, "http_client_request", 4096),
                            caller_pool,
                            socket,
                            lease,
                            peer_name,
                            method, uri,
                            std::move(headers), std::move(body), expect_100,
                            handler, cancel_ptr);
}
