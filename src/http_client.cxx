/*
 * HTTP client implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
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
#include "istream/istream_oo.hxx"
#include "istream/istream_pointer.hxx"
#include "istream/istream_cat.hxx"
#include "istream/istream_optional.hxx"
#include "istream/istream_chunked.hxx"
#include "istream/istream_dechunk.hxx"
#include "istream/istream_string.hxx"
#include "async.hxx"
#include "growing_buffer.hxx"
#include "please.hxx"
#include "uri/uri_verify.hxx"
#include "direct.hxx"
#include "stopwatch.hxx"
#include "strmap.hxx"
#include "completion.h"
#include "filtered_socket.hxx"
#include "pool.hxx"
#include "util/Cast.hxx"
#include "util/CharUtil.hxx"
#include "util/StringUtil.hxx"
#include "util/StringView.hxx"
#include "util/StaticArray.hxx"

#include <inline/compiler.h>
#include <inline/poison.h>
#include <daemon/log.h>

#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

/**
 * With a request body of this size or larger, we send "Expect:
 * 100-continue".
 */
static constexpr off_t EXPECT_100_THRESHOLD = 1024;

static constexpr struct timeval http_client_timeout = {
    .tv_sec = 30,
    .tv_usec = 0,
};

struct HttpClient final : IstreamHandler {
    enum class BucketResult {
        MORE,
        BLOCKING,
        DEPLETED,
        ERROR,
        DESTROYED,
    };

    struct ResponseBodyReader final : HttpBodyReader {
        explicit ResponseBodyReader(struct pool &_pool)
            :HttpBodyReader(_pool) {}

        HttpClient &GetClient() {
            return ContainerCast2(*this, &HttpClient::response_body_reader);
        }

        /* virtual methods from class Istream */

        off_t _GetAvailable(bool partial) override {
            return GetClient().GetAvailable(partial);
        }

        void _Read() override {
            GetClient().Read();
        }

        bool _FillBucketList(IstreamBucketList &list, GError **) override {
            GetClient().FillBucketList(list);
            return true;
        }

        size_t _ConsumeBucketList(size_t nbytes) override {
            return GetClient().ConsumeBucketList(nbytes);
        }

        int _AsFd() override {
            return GetClient().AsFD();
        }

        void _Close() override {
            GetClient().Close();
        }
    };

    struct pool &caller_pool;

    const char *const peer_name;

    struct stopwatch *const stopwatch;

    /* I/O */
    FilteredSocket socket;
    struct lease_ref lease_ref;

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

        struct http_response_handler_ref handler;

        Request():istream(nullptr) {}
    } request;

    struct async_operation request_async;

    /* response */
    struct response {
        enum {
            READ_STATUS,
            READ_HEADERS,
            READ_BODY,
        } read_state;

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
        struct strmap *headers;
        Istream *body;
    } response;

    ResponseBodyReader response_body_reader;

    /* connection settings */
    bool keep_alive;

    HttpClient(struct pool &_caller_pool, struct pool &_pool,
               int fd, FdType fd_type,
               Lease &lease,
               const char *_peer_name,
               const SocketFilter *filter, void *filter_ctx,
               http_method_t method, const char *uri,
               HttpHeaders &&headers,
               Istream *body, bool expect_100,
               const struct http_response_handler &handler,
               void *ctx,
               struct async_operation_ref &async_ref);

    ~HttpClient() {
        socket.Destroy();

        pool_unref(&caller_pool);
    }

    static HttpClient &FromAsync(struct async_operation &ao) {
        return ContainerCast2(ao, &HttpClient::request_async);
    }

    struct pool &GetPool() {
        return response_body_reader.GetPool();
    }

    gcc_pure
    bool IsValid() const {
        return socket.IsValid();
    }

    gcc_pure
    bool CheckDirect() const {
        assert(socket.GetType() == FdType::FD_NONE || socket.IsConnected());
        assert(response.read_state == response::READ_BODY);

        return response_body_reader.CheckDirect(socket.GetType());
    }

    void ScheduleWrite() {
        assert(socket.IsConnected());

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

        socket.Abandon();
        p_lease_release(lease_ref, reuse, GetPool());
    }

    /**
     * Release resources held by this object: the event object, the
     * socket lease, and the pool reference.
     */
    void Release(bool reuse) {
        stopwatch_dump(stopwatch);

        if (socket.IsConnected())
            ReleaseSocket(reuse);

        this->~HttpClient();
    }

    void PrefixError(GError **error_r) const {
        g_prefix_error(error_r, "error on HTTP connection to '%s': ",
                       peer_name);
    }

    void AbortResponseHeaders(GError *error);
    void AbortResponseBody(GError *error);
    void AbortResponse(GError *error);

    gcc_pure
    off_t GetAvailable(bool partial) const;

    void Read();

    void FillBucketList(IstreamBucketList &list);
    size_t ConsumeBucketList(size_t nbytes);

    int AsFD();
    void Close();

    /**
     * @return false if the connection has been closed
     */
    BucketResult TryWriteBuckets2(GError **error_r);
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

    void Abort();

    /* virtual methods from class IstreamHandler */
    size_t OnData(const void *data, size_t length) override;
    ssize_t OnDirect(FdType type, int fd, size_t max_length) override;
    void OnEof() override;
    void OnError(GError *error) override;
};

/**
 * Abort receiving the response status/headers from the HTTP server.
 */
void
HttpClient::AbortResponseHeaders(GError *error)
{
    assert(response.read_state == response::READ_STATUS ||
           response.read_state == response::READ_HEADERS);

    if (socket.IsConnected())
        ReleaseSocket(false);

    if (request.istream.IsDefined())
        request.istream.Close();

    PrefixError(&error);
    request.handler.InvokeAbort(error);
    Release(false);
}

/**
 * Abort receiving the response status/headers from the HTTP server.
 */
void
HttpClient::AbortResponseBody(GError *error)
{
    assert(response.read_state == response::READ_BODY);
    assert(response.body != nullptr);

    if (request.istream.IsDefined())
        request.istream.Close();

    PrefixError(&error);
    response_body_reader.InvokeError(error);
    Release(false);
}

/**
 * Abort receiving the response status/headers/body from the HTTP
 * server.
 */
void
HttpClient::AbortResponse(GError *error)
{
    assert(response.read_state == response::READ_STATUS ||
           response.read_state == response::READ_HEADERS ||
           response.read_state == response::READ_BODY);

    if (response.read_state != response::READ_BODY)
        AbortResponseHeaders(error);
    else
        AbortResponseBody(error);
}


/*
 * istream implementation for the response body
 *
 */

inline off_t
HttpClient::GetAvailable(bool partial) const
{
    assert(!socket.ended || response_body_reader.IsSocketDone(socket));
    assert(response.read_state == response::READ_BODY);
    assert(request.handler.IsUsed());

    return response_body_reader.GetAvailable(socket, partial);
}

inline void
HttpClient::Read()
{
    assert(!socket.ended || response_body_reader.IsSocketDone(socket));
    assert(response.read_state == response::READ_BODY);
    assert(response_body_reader.HasHandler());
    assert(request.handler.IsUsed());

    if (response_body_reader.IsEOF()) {
        /* just in case EOF has been reached by ConsumeBucketList() */
        ResponseBodyEOF();
        return;
    }

    if (socket.IsConnected())
        socket.base.SetDirect(CheckDirect());

    if (response.in_handler)
        /* avoid recursion; the http_response_handler caller will
           continue parsing the response if possible */
        return;

    socket.Read(response_body_reader.RequireMore());
}

inline void
HttpClient::FillBucketList(IstreamBucketList &list)
{
    assert(!socket.ended || response_body_reader.IsSocketDone(socket));
    assert(response.read_state == response::READ_BODY);
    assert(request.handler.IsUsed());

    response_body_reader.FillBucketList(socket, list);
}

inline size_t
HttpClient::ConsumeBucketList(size_t nbytes)
{
    assert(!socket.ended || response_body_reader.IsSocketDone(socket));
    assert(response.read_state == response::READ_BODY);
    assert(request.handler.IsUsed());

    return response_body_reader.ConsumeBucketList(socket, nbytes);
}

inline int
HttpClient::AsFD()
{
    assert(!socket.ended || response_body_reader.IsSocketDone(socket));
    assert(response.read_state == response::READ_BODY);
    assert(request.handler.IsUsed());

    if (!socket.IsConnected() ||
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
    assert(response.read_state == response::READ_BODY);
    assert(request.handler.IsUsed());

    stopwatch_event(stopwatch, "close");

    if (request.istream.IsDefined())
        request.istream.Close();

    Release(false);
}

inline HttpClient::BucketResult
HttpClient::TryWriteBuckets2(GError **error_r)
{
    if (socket.HasFilter())
        return BucketResult::MORE;

    IstreamBucketList list;
    if (!request.istream.FillBucketList(list, error_r)) {
        request.istream.Clear();
        return BucketResult::ERROR;
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

        g_set_error(error_r, http_client_quark(), HTTP_CLIENT_IO,
                    "write error (%s)", strerror(_errno));
        return BucketResult::ERROR;
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
    GError *error = nullptr;
    auto result = TryWriteBuckets2(&error);
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
        socket.ScheduleReadTimeout(true, &http_client_timeout);
        break;

    case BucketResult::ERROR:
        assert(!request.istream.IsDefined());
        stopwatch_event(stopwatch, "error");
        AbortResponse(error);
        result = BucketResult::DESTROYED;
        break;

    case BucketResult::DESTROYED:
        break;
    }

    return result;
}

inline bool
HttpClient::ParseStatusLine(const char *line, size_t length)
{
    assert(response.read_state == response::READ_STATUS);

    const char *space;
    if (length < 10 || memcmp(line, "HTTP/", 5) != 0 ||
        (space = (const char *)memchr(line + 6, ' ', length - 6)) == nullptr) {
        stopwatch_event(stopwatch, "malformed");

        GError *error =
            g_error_new_literal(http_client_quark(), HTTP_CLIENT_GARBAGE,
                                "malformed HTTP status line");
        AbortResponseHeaders(error);
        return false;
    }

    response.http_1_0 = line[7] == '0' && line[6] == '.' && line[5] == '1';

    length = line + length - space - 1;
    line = space + 1;

    if (unlikely(length < 3 || !IsDigitASCII(line[0]) ||
                 !IsDigitASCII(line[1]) || !IsDigitASCII(line[2]))) {
        stopwatch_event(stopwatch, "malformed");

        GError *error =
            g_error_new_literal(http_client_quark(), HTTP_CLIENT_GARBAGE,
                                "no HTTP status found");
        AbortResponseHeaders(error);
        return false;
    }

    response.status = (http_status_t)(((line[0] - '0') * 10 + line[1] - '0') * 10 + line[2] - '0');
    if (gcc_unlikely(!http_status_is_valid(response.status))) {
        stopwatch_event(stopwatch, "malformed");

        GError *error =
            g_error_new(http_client_quark(), HTTP_CLIENT_GARBAGE,
                        "invalid HTTP status %d",
                        response.status);
        AbortResponseHeaders(error);
        return false;
    }

    response.read_state = response::READ_HEADERS;
    response.headers = strmap_new(&caller_pool);
    return true;
}

inline bool
HttpClient::HeadersFinished()
{
    stopwatch_event(stopwatch, "headers");

    auto &response_headers = *response.headers;

    const char *header_connection = response_headers.Remove("connection");
    keep_alive =
        (header_connection == nullptr && !response.http_1_0) ||
        (header_connection != nullptr &&
         http_list_contains_i(header_connection, "keep-alive"));

    if (http_status_is_empty(response.status) ||
        response.no_body) {
        response.body = nullptr;
        response.read_state = response::READ_BODY;
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

        if (unlikely(content_length_string == nullptr)) {
            if (keep_alive) {
                stopwatch_event(stopwatch, "malformed");

                GError *error =
                    g_error_new_literal(http_client_quark(),
                                        HTTP_CLIENT_UNSPECIFIED,
                                        "no Content-Length header response");
                AbortResponseHeaders(error);
                return false;
            }
            content_length = (off_t)-1;
        } else {
            char *endptr;
            content_length = (off_t)strtoull(content_length_string,
                                             &endptr, 10);
            if (unlikely(endptr == content_length_string || *endptr != 0 ||
                         content_length < 0)) {
                stopwatch_event(stopwatch, "malformed");

                GError *error =
                    g_error_new_literal(http_client_quark(),
                                        HTTP_CLIENT_UNSPECIFIED,
                                        "invalid Content-Length header in response");
                AbortResponseHeaders(error);
                return false;
            }

            if (content_length == 0) {
                response.body = nullptr;
                response.read_state = response::READ_BODY;
                return true;
            }
        }

        chunked = false;
    } else {
        /* chunked */

        content_length = (off_t)-1;
        chunked = true;
    }

    response.body = &response_body_reader.Init(content_length,
                                               chunked);

    response.read_state = response::READ_BODY;
    socket.base.SetDirect(CheckDirect());
    return true;
}

inline bool
HttpClient::HandleLine(const char *line, size_t length)
{
    assert(response.read_state == response::READ_STATUS ||
           response.read_state == response::READ_HEADERS);

    if (response.read_state == response::READ_STATUS)
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
    assert(client->response.read_state == HttpClient::response::READ_BODY);
    assert(client->request.handler.IsUsed());

    stopwatch_event(client->stopwatch, "end");

    if (!client->socket.IsEmpty()) {
        daemon_log(2, "excess data after HTTP response\n");
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
    assert(response.read_state == response::READ_STATUS ||
           response.read_state == response::READ_HEADERS);
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

        if (response.read_state != response::READ_HEADERS) {
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
    assert(response.read_state == response::READ_BODY);
    assert(request.handler.IsUsed());
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
    assert(response.read_state == response::READ_BODY);

    size_t nbytes = response_body_reader.FeedBody(data, length);
    if (nbytes == 0)
        return socket.IsValid()
            ? BufferedResult::BLOCKING
            : BufferedResult::CLOSED;

    socket.Consumed(nbytes);

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
    assert(response.read_state == response::READ_STATUS ||
           response.read_state == response::READ_HEADERS);

    const BufferedResult result = ParseHeaders(data, length);
    if (result != BufferedResult::AGAIN_EXPECT)
        return result;

    /* the headers are finished, we can now report the response to
       the handler */
    assert(response.read_state == response::READ_BODY);

    if (response.status == HTTP_STATUS_CONTINUE) {
        assert(response.body == nullptr);

        if (request.body == nullptr) {
            GError *error = g_error_new_literal(http_client_quark(),
                                                HTTP_CLIENT_UNSPECIFIED,
                                                "unexpected status 100");
#ifndef NDEBUG
            /* assertion workaround */
            response.read_state = response::READ_STATUS;
#endif
            AbortResponseHeaders(error);
            return BufferedResult::CLOSED;
        }

        /* reset read_state, we're now expecting the real response */
        response.read_state = response::READ_STATUS;

        istream_optional_resume(*request.body);
        request.body = nullptr;

        if (!socket.IsConnected()) {
            GError *error = g_error_new_literal(http_client_quark(),
                                                HTTP_CLIENT_UNSPECIFIED,
                                                "Peer closed the socket prematurely after status 100");
#ifndef NDEBUG
            /* assertion workaround */
            response.read_state = HttpClient::response::READ_STATUS;
#endif
            AbortResponseHeaders(error);
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
        socket.IsConnected())
        /* we don't need the socket anymore, we've got everything we
           need in the input buffer */
        ReleaseSocket(keep_alive);

    const ScopePoolRef ref(GetPool() TRACE_ARGS);
    const ScopePoolRef caller_ref(caller_pool TRACE_ARGS);

    response.in_handler = true;
    request.handler.InvokeResponse(response.status, response.headers,
                                   response.body);
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
    assert(socket.IsConnected());
    assert(response.read_state == response::READ_BODY);
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
    switch (response.read_state) {
    case response::READ_STATUS:
    case response::READ_HEADERS:
        return FeedHeaders(data, length);

    case response::READ_BODY:
        assert(response.body != nullptr);

        if (socket.IsConnected() && response_body_reader.IsSocketDone(socket))
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

static BufferedResult
http_client_socket_data(const void *buffer, size_t size, void *ctx)
{
    HttpClient *client = (HttpClient *)ctx;

    const ScopePoolRef ref(client->GetPool() TRACE_ARGS);
    return client->Feed(buffer, size);
}

static DirectResult
http_client_socket_direct(int fd, FdType fd_type, void *ctx)
{
    HttpClient *client = (HttpClient *)ctx;

    return client->TryResponseDirect(fd, fd_type);

}

static bool
http_client_socket_closed(void *ctx)
{
    HttpClient *client = (HttpClient *)ctx;

    stopwatch_event(client->stopwatch, "end");

    if (client->request.istream.IsDefined())
        client->request.istream.ClearAndClose();

    /* can't reuse the socket, it was closed by the peer */
    client->ReleaseSocket(false);

    return true;
}

static bool
http_client_socket_remaining(size_t remaining, void *ctx)
{
    HttpClient *client = (HttpClient *)ctx;

    if (client->response.read_state < HttpClient::response::READ_BODY)
        /* this information comes too early, we can't use it */
        return true;

    if (client->response_body_reader.SocketEOF(remaining)) {
        /* there's data left in the buffer: continue serving the
           buffer */
        return true;
    } else {
        /* finished: close the HTTP client */
        client->Release(false);
        return false;
    }
}

static bool
http_client_socket_write(void *ctx)
{
    HttpClient *client = (HttpClient *)ctx;

    client->request.got_data = false;

    switch (client->TryWriteBuckets()) {
    case HttpClient::BucketResult::MORE:
        break;

    case HttpClient::BucketResult::BLOCKING:
    case HttpClient::BucketResult::DEPLETED:
        return true;

    case HttpClient::BucketResult::ERROR:
    case HttpClient::BucketResult::DESTROYED:
        return false;
    }

    const ScopePoolRef ref(client->GetPool() TRACE_ARGS);

    client->request.istream.Read();

    const bool result = client->socket.IsValid() &&
        client->socket.IsConnected();
    if (result && client->request.istream.IsDefined()) {
        if (client->request.got_data)
            client->ScheduleWrite();
        else
            client->socket.UnscheduleWrite();
    }

    return result;
}

static enum write_result
http_client_socket_broken(void *ctx)
{
    HttpClient *client = (HttpClient *)ctx;

    /* the server has closed the connection, probably because he's not
       interested in our request body - that's ok; now we wait for his
       response */

    client->keep_alive = false;

    if (client->request.istream.IsDefined())
        client->request.istream.ClearAndClose();

    client->socket.ScheduleReadTimeout(true, &http_client_timeout);

    return WRITE_BROKEN;
}

static void
http_client_socket_error(GError *error, void *ctx)
{
    HttpClient *client = (HttpClient *)ctx;

    stopwatch_event(client->stopwatch, "error");
    client->AbortResponse(error);
}

static constexpr BufferedSocketHandler http_client_socket_handler = {
    .data = http_client_socket_data,
    .direct = http_client_socket_direct,
    .closed = http_client_socket_closed,
    .remaining = http_client_socket_remaining,
    .end = nullptr,
    .write = http_client_socket_write,
    .drained = nullptr,
    .timeout = nullptr,
    .broken = http_client_socket_broken,
    .error = http_client_socket_error,
};


/*
 * istream handler for the request
 *
 */

inline size_t
HttpClient::OnData(const void *data, size_t length)
{
    assert(socket.IsConnected());

    request.got_data = true;

    ssize_t nbytes = socket.Write(data, length);
    if (likely(nbytes >= 0)) {
        ScheduleWrite();
        return (size_t)nbytes;
    }

    if (gcc_likely(nbytes == WRITE_BLOCKING || nbytes == WRITE_DESTROYED ||
                   nbytes == WRITE_BROKEN))
        return 0;

    int _errno = errno;

    stopwatch_event(stopwatch, "error");

    GError *error = g_error_new(http_client_quark(), HTTP_CLIENT_IO,
                                "write error (%s)", strerror(_errno));
    AbortResponse(error);
    return 0;
}

inline ssize_t
HttpClient::OnDirect(FdType type, int fd, size_t max_length)
{
    assert(socket.IsConnected());

    request.got_data = true;

    ssize_t nbytes = socket.WriteFrom(fd, type, max_length);
    if (likely(nbytes > 0))
        ScheduleWrite();
    else if (nbytes == WRITE_BLOCKING)
        return ISTREAM_RESULT_BLOCKING;
    else if (nbytes == WRITE_DESTROYED || nbytes == WRITE_BROKEN)
        return ISTREAM_RESULT_CLOSED;
    else if (likely(nbytes < 0)) {
        if (gcc_likely(errno == EAGAIN)) {
            request.got_data = false;
            socket.UnscheduleWrite();
        }
    }

    return nbytes;
}

inline void
HttpClient::OnEof()
{
    stopwatch_event(stopwatch, "request");

    assert(request.istream.IsDefined());
    request.istream.Clear();

    socket.UnscheduleWrite();
    socket.Read(false);
}

inline void
HttpClient::OnError(GError *error)
{
    assert(response.read_state == response::READ_STATUS ||
           response.read_state == response::READ_HEADERS ||
           response.read_state == response::READ_BODY);

    stopwatch_event(stopwatch, "abort");

    assert(request.istream.IsDefined());
    request.istream.Clear();

    if (response.read_state != HttpClient::response::READ_BODY)
        AbortResponseHeaders(error);
    else if (response.body != nullptr)
        AbortResponseBody(error);
    else
        g_error_free(error);
}

/*
 * async operation
 *
 */

inline void
HttpClient::Abort()
{
    stopwatch_event(stopwatch, "abort");

    /* async_operation_ref::Abort() can only be used before the
       response was delivered to our callback */
    assert(response.read_state == response::READ_STATUS ||
           response.read_state == response::READ_HEADERS);

    if (request.istream.IsDefined())
        request.istream.Close();

    Release(false);
}

static void
http_client_request_abort(struct async_operation *ao)
{
    HttpClient &client = HttpClient::FromAsync(*ao);

    client.Abort();
}

static const struct async_operation_class http_client_async_operation = {
    .abort = http_client_request_abort,
};


/*
 * constructor
 *
 */

inline
HttpClient::HttpClient(struct pool &_caller_pool, struct pool &_pool,
                       int fd, FdType fd_type,
                       Lease &lease,
                       const char *_peer_name,
                       const SocketFilter *filter, void *filter_ctx,
                       http_method_t method, const char *uri,
                       HttpHeaders &&headers,
                       Istream *body, bool expect_100,
                       const struct http_response_handler &handler,
                       void *ctx,
                       struct async_operation_ref &async_ref)
    :caller_pool(_caller_pool),
     peer_name(_peer_name),
     stopwatch(stopwatch_fd_new(&_pool, fd, uri)),
     response_body_reader(_pool)
{
    socket.Init(GetPool(), fd, fd_type,
                &http_client_timeout, &http_client_timeout,
                filter, filter_ctx,
                http_client_socket_handler, this);
    p_lease_ref_set(lease_ref, lease,
                    GetPool(), "http_client_lease");

    response.read_state = HttpClient::response::READ_STATUS;
    response.no_body = http_method_is_empty(method);

    pool_ref(&caller_pool);
    request.handler.Set(handler, ctx);

    request_async.Init(http_client_async_operation);
    async_ref.Set(request_async);

    /* request line */

    const char *p = p_strcat(&GetPool(),
                             http_method_to_string(method), " ", uri,
                             " HTTP/1.1\r\n", nullptr);
    Istream *request_line_stream = istream_string_new(&GetPool(), p);

    /* headers */

    GrowingBuffer &headers2 = headers.MakeBuffer(GetPool());

    const bool upgrade = body != nullptr && http_is_upgrade(headers);
    if (upgrade) {
        /* forward hop-by-hop headers requesting the protocol
           upgrade */
        headers.Write(GetPool(), "connection", "upgrade");
        headers.MoveToBuffer(GetPool(), "upgrade");
        request.body = nullptr;
    } else if (body != nullptr) {
        off_t content_length = body->GetAvailable(false);
        if (content_length == (off_t)-1) {
            header_write(&headers2, "transfer-encoding", "chunked");

            /* optimized code path: if an istream_dechunked shall get
               chunked via istream_chunk, let's just skip both to
               reduce the amount of work and I/O we have to do */
            if (!istream_dechunk_check_verbatim(*body))
                body = istream_chunked_new(GetPool(), *body);
        } else {
            snprintf(request.content_length_buffer,
                     sizeof(request.content_length_buffer),
                     "%lu", (unsigned long)content_length);
            header_write(&headers2, "content-length",
                         request.content_length_buffer);
        }

        off_t available = expect_100 ? body->GetAvailable(true) : 0;
        if (available < 0 || available >= EXPECT_100_THRESHOLD) {
            /* large request body: ask the server for confirmation
               that he's really interested */
            header_write(&headers2, "expect", "100-continue");
            body = request.body = istream_optional_new(GetPool(), *body);
        } else
            /* short request body: send it immediately */
            request.body = nullptr;
    } else
        request.body = nullptr;

    GrowingBuffer &headers3 = headers.ToBuffer(GetPool());
    growing_buffer_write_buffer(&headers3, "\r\n", 2);

    Istream *header_stream = istream_gb_new(GetPool(), headers3);

    /* request istream */

    request.istream.Set(*istream_cat_new(GetPool(),
                                         request_line_stream,
                                         header_stream,
                                         body),
                        *this, socket.GetDirectMask());

    socket.ScheduleReadNoTimeout(true);

    switch (TryWriteBuckets()) {
    case HttpClient::BucketResult::MORE:
        request.istream.Read();
        break;

    case HttpClient::BucketResult::BLOCKING:
    case HttpClient::BucketResult::DEPLETED:
    case HttpClient::BucketResult::ERROR:
    case HttpClient::BucketResult::DESTROYED:
        break;
    }
}

void
http_client_request(struct pool &caller_pool,
                    int fd, FdType fd_type,
                    Lease &lease,
                    const char *peer_name,
                    const SocketFilter *filter, void *filter_ctx,
                    http_method_t method, const char *uri,
                    HttpHeaders &&headers,
                    Istream *body, bool expect_100,
                    const struct http_response_handler &handler,
                    void *ctx,
                    struct async_operation_ref &async_ref)
{
    assert(fd >= 0);
    assert(http_method_is_valid(method));
    assert(handler.response != nullptr);

    if (!uri_path_verify_quick(uri)) {
        lease.ReleaseLease(true);
        if (body != nullptr)
            body->CloseUnused();

        GError *error = g_error_new(http_client_quark(),
                                    HTTP_CLIENT_UNSPECIFIED,
                                    "malformed request URI '%s'", uri);
        handler.InvokeAbort(ctx, error);
        return;
    }

    struct pool *pool =
        pool_new_linear(&caller_pool, "http_client_request", 8192);

    NewFromPool<HttpClient>(*pool, caller_pool, *pool,
                            fd, fd_type,
                            lease,
                            peer_name,
                            filter, filter_ctx,
                            method, uri,
                            std::move(headers), body, expect_100,
                            handler, ctx, async_ref);
    pool_unref(pool); // response_body_reader holds the reference
}
