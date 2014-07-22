/*
 * HTTP client implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http_client.hxx"
#include "http_response.hxx"
#include "fifo-buffer.h"
#include "strutil.h"
#include "header_parser.hxx"
#include "header_writer.hxx"
#include "pevent.h"
#include "http_body.hxx"
#include "istream-internal.h"
#include "istream_gb.hxx"
#include "async.hxx"
#include "growing_buffer.hxx"
#include "please.h"
#include "uri-verify.h"
#include "direct.h"
#include "stopwatch.h"
#include "strmap.hxx"
#include "completion.h"
#include "filtered_socket.hxx"
#include "pool.hxx"
#include "util/Cast.hxx"

#include <inline/compiler.h>
#include <inline/poison.h>
#include <daemon/log.h>
#include <socket/address.h>

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

struct http_client {
    struct pool *pool, *caller_pool;

    const char *peer_name;

    struct stopwatch *stopwatch;

    /* I/O */
    FilteredSocket socket;
    struct lease_ref lease_ref;

    /* request */
    struct {
        /**
         * An "istream_optional" which blocks sending the request body
         * until the server has confirmed "100 Continue".
         */
        struct istream *body;

        struct istream *istream;
        char content_length_buffer[32];

        /**
         * This flag is set when the request istream has submitted
         * data.  It is used to check whether the request istream is
         * unavailable, to unschedule the socket write event.
         */
        bool got_data;

        struct http_response_handler_ref handler;
        struct async_operation async;
    } request;

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
        struct istream *body;
        HttpBodyReader body_reader;
    } response;

    /* connection settings */
    bool keep_alive;

    static struct http_client &FromResponseBody(struct istream &istream) {
        auto &body = HttpBodyReader::FromStream(istream);
        return *ContainerCast(&body, struct http_client,
                              response.body_reader);
    }

    static struct http_client &FromAsync(struct async_operation &ao) {
        return *ContainerCast(&ao, struct http_client, request.async);
    }

    gcc_pure
    bool IsValid() const {
        return socket.IsValid();
    }

    gcc_pure
    bool CheckDirect() const {
        assert(socket.GetType() == ISTREAM_NONE || socket.IsConnected());
        assert(response.read_state == response::READ_BODY);

        return response.body_reader.CheckDirect(socket.GetType());
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
        p_lease_release(&lease_ref, reuse, pool);
    }

    /**
     * Release resources held by this object: the event object, the
     * socket lease, and the pool reference.
     */
    void Release(bool reuse) {
        stopwatch_dump(stopwatch);

        if (socket.IsConnected())
            ReleaseSocket(reuse);

        socket.Destroy();

        pool_unref(caller_pool);
        pool_unref(pool);
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
    int AsFD();
    void Close();

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

    DirectResult TryResponseDirect(int fd, enum istream_direct fd_type);

    void Abort();
};

static const char *
get_peer_name(int fd)
{
     struct sockaddr_storage address;
     socklen_t address_length = sizeof(address);

     static char buffer[64];
     if (getpeername(fd, (struct sockaddr *)&address, &address_length) < 0 ||
         !socket_address_to_string(buffer, sizeof(buffer),
                                   (const struct sockaddr *)&address,
                                   address_length))
         return "unknown";

     return buffer;
}

/**
 * Abort receiving the response status/headers from the HTTP server.
 */
void
http_client::AbortResponseHeaders(GError *error)
{
    assert(response.read_state == response::READ_STATUS ||
           response.read_state == response::READ_HEADERS);

    if (socket.IsConnected())
        ReleaseSocket(false);

    if (request.istream != nullptr)
        istream_close_handler(request.istream);

    PrefixError(&error);
    http_response_handler_invoke_abort(&request.handler, error);
    Release(false);
}

/**
 * Abort receiving the response status/headers from the HTTP server.
 */
void
http_client::AbortResponseBody(GError *error)
{
    assert(response.read_state == response::READ_BODY);
    assert(response.body != nullptr);

    if (request.istream != nullptr)
        istream_close_handler(request.istream);

    PrefixError(&error);
    response.body_reader.DeinitAbort(error);
    Release(false);
}

/**
 * Abort receiving the response status/headers/body from the HTTP
 * server.
 */
void
http_client::AbortResponse(GError *error)
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
http_client::GetAvailable(bool partial) const
{
    assert(!socket.ended || response.body_reader.IsSocketDone(socket));
    assert(response.read_state == response::READ_BODY);
    assert(http_response_handler_used(&request.handler));

    return response.body_reader.GetAvailable(socket, partial);
}

static off_t
http_client_response_stream_available(struct istream *istream, bool partial)
{
    struct http_client &client = http_client::FromResponseBody(*istream);

    return client.GetAvailable(partial);
}

inline void
http_client::Read()
{
    assert(!socket.ended || response.body_reader.IsSocketDone(socket));
    assert(response.read_state == response::READ_BODY);
    assert(istream_has_handler(&response.body_reader.GetStream()));
    assert(http_response_handler_used(&request.handler));

    if (response.in_handler)
        /* avoid recursion; the http_response_handler caller will
           continue parsing the response if possible */
        return;

    if (socket.IsConnected())
        socket.base.direct = CheckDirect();

    socket.Read(response.body_reader.RequireMore());
}

static void
http_client_response_stream_read(struct istream *istream)
{
    struct http_client &client = http_client::FromResponseBody(*istream);

    client.Read();
}

inline int
http_client::AsFD()
{
    assert(!socket.ended || response.body_reader.IsSocketDone(socket));
    assert(response.read_state == response::READ_BODY);
    assert(http_response_handler_used(&request.handler));

    if (!socket.IsConnected() ||
        keep_alive ||
        /* must not be chunked */
        &response.body_reader.GetStream() != response.body)
        return -1;

    int fd = socket.AsFD();
    if (fd < 0)
        return -1;

    response.body_reader.Deinit();
    Release(false);
    return fd;
}

static int
http_client_response_stream_as_fd(struct istream *istream)
{
    struct http_client &client = http_client::FromResponseBody(*istream);

    return client.AsFD();
}

inline void
http_client::Close()
{
    assert(response.read_state == response::READ_BODY);
    assert(http_response_handler_used(&request.handler));
    assert(!response.body_reader.IsEOF());

    stopwatch_event(stopwatch, "close");

    if (request.istream != nullptr)
        istream_close_handler(request.istream);

    response.body_reader.Deinit();
    Release(false);
}

static void
http_client_response_stream_close(struct istream *istream)
{
    struct http_client &client = http_client::FromResponseBody(*istream);

    client.Close();
}

static const struct istream_class http_client_response_stream = {
    .available = http_client_response_stream_available,
    .read = http_client_response_stream_read,
    .as_fd = http_client_response_stream_as_fd,
    .close = http_client_response_stream_close,
};

inline bool
http_client::ParseStatusLine(const char *line, size_t length)
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

    if (unlikely(length < 3 || !char_is_digit(line[0]) ||
                 !char_is_digit(line[1]) || !char_is_digit(line[2]))) {
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
    response.headers = strmap_new(caller_pool);
    return true;
}

inline bool
http_client::HeadersFinished()
{
    stopwatch_event(stopwatch, "headers");

    auto &response_headers = *response.headers;

    const char *header_connection = response_headers.Remove("connection");
    keep_alive =
        (header_connection == nullptr && !response.http_1_0) ||
        (header_connection != nullptr &&
         strcasecmp(header_connection, "keep-alive") == 0);

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
    response_headers.Remove("upgrade");

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

    response.body = &response.body_reader.Init(http_client_response_stream,
                                               *pool,
                                               *pool,
                                               content_length,
                                               chunked);

    response.read_state = response::READ_BODY;
    socket.base.direct = CheckDirect();
    return true;
}

inline bool
http_client::HandleLine(const char *line, size_t length)
{
    assert(response.read_state == response::READ_STATUS ||
           response.read_state == response::READ_HEADERS);

    if (response.read_state == response::READ_STATUS)
        return ParseStatusLine(line, length);
    else if (length > 0) {
        header_parse_line(pool, response.headers,
                          line, length);
        return true;
    } else
        return HeadersFinished();
}

static void
http_client_response_finished(struct http_client *client)
{
    assert(client->response.read_state == http_client::response::READ_BODY);
    assert(http_response_handler_used(&client->request.handler));

    stopwatch_event(client->stopwatch, "end");

    if (!client->socket.IsEmpty()) {
        daemon_log(2, "excess data after HTTP response\n");
        client->keep_alive = false;
    }

    if (client->request.istream != nullptr)
        istream_close_handler(client->request.istream);

    client->Release(client->keep_alive &&
                        client->request.istream == nullptr);
}

inline BufferedResult
http_client::ParseHeaders(const void *_data, size_t length)
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
        --end;
        while (end >= start && char_is_whitespace(*end))
            --end;

        /* handle this line */
        if (!HandleLine(start, end - start + 1))
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
http_client::ResponseBodyEOF()
{
    assert(response.read_state == response::READ_BODY);
    assert(http_response_handler_used(&request.handler));
    assert(response.body_reader.IsEOF());

    /* this pointer must be cleared before forwarding the EOF event to
       our response body handler.  If we forget that, the handler
       might close the request body, leading to an assertion failure
       because http_client_request_stream_abort() calls
       http_client_abort_response_body(), not knowing that the
       response body is already finished  */
    response.body = nullptr;

    response.body_reader.DeinitEOF();

    http_client_response_finished(this);
}

inline BufferedResult
http_client::FeedBody(const void *data, size_t length)
{
    assert(response.read_state == response::READ_BODY);

    size_t nbytes = response.body_reader.FeedBody(data, length);
    if (nbytes == 0)
        return socket.IsValid()
            ? BufferedResult::BLOCKING
            : BufferedResult::CLOSED;

    socket.Consumed(nbytes);

    if (response.body_reader.IsEOF()) {
        ResponseBodyEOF();
        return BufferedResult::CLOSED;
    }

    if (nbytes < length)
        return BufferedResult::PARTIAL;

    if (response.body_reader.RequireMore())
        return BufferedResult::MORE;

    return BufferedResult::OK;
}

BufferedResult
http_client::FeedHeaders(const void *data, size_t length)
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

        istream_optional_resume(request.body);
        request.body = nullptr;

        if (!socket.IsConnected()) {
            GError *error = g_error_new_literal(http_client_quark(),
                                                HTTP_CLIENT_UNSPECIFIED,
                                                "Peer closed the socket prematurely after status 100");
#ifndef NDEBUG
            /* assertion workaround */
            response.read_state = http_client::response::READ_STATUS;
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
        istream_optional_discard(request.body);
        request.body = nullptr;
    }

    if ((response.body == nullptr ||
         response.body_reader.IsSocketDone(socket)) &&
        socket.IsConnected())
        /* we don't need the socket anymore, we've got everything we
           need in the input buffer */
        ReleaseSocket(keep_alive);

    pool_ref(pool);
    pool_ref(caller_pool);

    response.in_handler = true;
    http_response_handler_invoke_response(&request.handler,
                                          response.status,
                                          response.headers,
                                          response.body);
    response.in_handler = false;

    const bool valid = IsValid();
    pool_unref(caller_pool);
    pool_unref(pool);

    if (!valid)
        return BufferedResult::CLOSED;

    if (response.body == nullptr) {
        http_client_response_finished(this);
        return BufferedResult::CLOSED;
    }

    /* now do the response body */
    return response.body_reader.RequireMore()
        ? BufferedResult::AGAIN_EXPECT
        : BufferedResult::AGAIN_OPTIONAL;
}

inline DirectResult
http_client::TryResponseDirect(int fd, enum istream_direct fd_type)
{
    assert(socket.IsConnected());
    assert(response.read_state == response::READ_BODY);
    assert(CheckDirect());

    ssize_t nbytes = response.body_reader.TryDirect(fd, fd_type);
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
        if (request.istream != nullptr)
            istream_close_handler(request.istream);

        response.body_reader.SocketEOF(0);
        Release(false);
        return DirectResult::CLOSED;
   }

    if (response.body_reader.IsEOF()) {
        ResponseBodyEOF();
        return DirectResult::CLOSED;
    }

    return DirectResult::OK;
}

inline BufferedResult
http_client::Feed(const void *data, size_t length)
{
    switch (response.read_state) {
    case response::READ_STATUS:
    case response::READ_HEADERS:
        return FeedHeaders(data, length);

    case response::READ_BODY:
        assert(response.body != nullptr);

        if (socket.IsConnected() && response.body_reader.IsSocketDone(socket))
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
    struct http_client *client = (struct http_client *)ctx;

    const ScopePoolRef ref(*client->pool TRACE_ARGS);
    return client->Feed(buffer, size);
}

static DirectResult
http_client_socket_direct(int fd, enum istream_direct fd_type, void *ctx)
{
    struct http_client *client = (struct http_client *)ctx;

    return client->TryResponseDirect(fd, fd_type);

}

static bool
http_client_socket_closed(void *ctx)
{
    struct http_client *client = (struct http_client *)ctx;

    stopwatch_event(client->stopwatch, "end");

    if (client->request.istream != nullptr)
        istream_free(&client->request.istream);

    /* can't reuse the socket, it was closed by the peer */
    client->ReleaseSocket(false);

    return true;
}

static bool
http_client_socket_remaining(size_t remaining, void *ctx)
{
    struct http_client *client = (struct http_client *)ctx;

    if (client->response.read_state < http_client::response::READ_BODY)
        /* this information comes too early, we can't use it */
        return true;

    if (client->response.body_reader.SocketEOF(remaining)) {
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
    struct http_client *client = (struct http_client *)ctx;

    const ScopePoolRef ref(*client->pool TRACE_ARGS);

    client->request.got_data = false;
    istream_read(client->request.istream);

    const bool result = client->socket.IsValid() &&
        client->socket.IsConnected();
    if (result && client->request.istream != nullptr) {
        if (client->request.got_data)
            client->ScheduleWrite();
        else
            client->socket.UnscheduleWrite();
    }

    return result;
}

static bool
http_client_socket_broken(void *ctx)
{
    struct http_client *client = (struct http_client *)ctx;

    /* the server has closed the connection, probably because he's not
       interested in our request body - that's ok; now we wait for his
       response */

    client->keep_alive = false;

    if (client->request.istream != nullptr)
        istream_free(&client->request.istream);

    client->socket.ScheduleReadTimeout(true, &http_client_timeout);
    return true;
}

static void
http_client_socket_error(GError *error, void *ctx)
{
    struct http_client *client = (struct http_client *)ctx;

    stopwatch_event(client->stopwatch, "error");
    client->AbortResponse(error);
}

static constexpr BufferedSocketHandler http_client_socket_handler = {
    .data = http_client_socket_data,
    .direct = http_client_socket_direct,
    .closed = http_client_socket_closed,
    .remaining = http_client_socket_remaining,
    .write = http_client_socket_write,
    .broken = http_client_socket_broken,
    .error = http_client_socket_error,
};


/*
 * istream handler for the request
 *
 */

static size_t
http_client_request_stream_data(const void *data, size_t length, void *ctx)
{
    struct http_client *client = (struct http_client *)ctx;

    assert(client->socket.IsConnected());

    client->request.got_data = true;

    ssize_t nbytes = client->socket.Write(data, length);
    if (likely(nbytes >= 0)) {
        client->ScheduleWrite();
        return (size_t)nbytes;
    }

    if (gcc_likely(nbytes == WRITE_BLOCKING || nbytes == WRITE_DESTROYED ||
                   nbytes == WRITE_BROKEN))
        return 0;

    int _errno = errno;

    stopwatch_event(client->stopwatch, "error");

    GError *error = g_error_new(http_client_quark(), HTTP_CLIENT_IO,
                                "write error (%s)", strerror(_errno));
    client->AbortResponse(error);
    return 0;
}

static ssize_t
http_client_request_stream_direct(istream_direct type, int fd,
                                  size_t max_length, void *ctx)
{
    struct http_client *client = (struct http_client *)ctx;

    assert(client->socket.IsConnected());

    client->request.got_data = true;

    ssize_t nbytes = client->socket.WriteFrom(fd, type, max_length);
    if (likely(nbytes > 0))
        client->ScheduleWrite();
    else if (nbytes == WRITE_BLOCKING)
        return ISTREAM_RESULT_BLOCKING;
    else if (nbytes == WRITE_DESTROYED || nbytes == WRITE_BROKEN)
        return ISTREAM_RESULT_CLOSED;
    else if (likely(nbytes < 0)) {
        if (gcc_likely(errno == EAGAIN)) {
            client->request.got_data = false;
            client->socket.UnscheduleWrite();
        }
    }

    return nbytes;
}

static void
http_client_request_stream_eof(void *ctx)
{
    struct http_client *client = (struct http_client *)ctx;

    stopwatch_event(client->stopwatch, "request");

    assert(client->request.istream != nullptr);
    client->request.istream = nullptr;

    client->socket.UnscheduleWrite();
    client->socket.Read(false);
}

static void
http_client_request_stream_abort(GError *error, void *ctx)
{
    struct http_client *client = (struct http_client *)ctx;

    assert(client->response.read_state == http_client::response::READ_STATUS ||
           client->response.read_state == http_client::response::READ_HEADERS ||
           client->response.read_state == http_client::response::READ_BODY);

    stopwatch_event(client->stopwatch, "abort");

    client->request.istream = nullptr;

    if (client->response.read_state != http_client::response::READ_BODY)
        client->AbortResponseHeaders(error);
    else if (client->response.body != nullptr)
        client->AbortResponseBody(error);
    else
        g_error_free(error);
}

static const struct istream_handler http_client_request_stream_handler = {
    .data = http_client_request_stream_data,
    .direct = http_client_request_stream_direct,
    .eof = http_client_request_stream_eof,
    .abort = http_client_request_stream_abort,
};


/*
 * async operation
 *
 */

inline void
http_client::Abort()
{
    stopwatch_event(stopwatch, "abort");

    /* async_operation_ref::Abort() can only be used before the
       response was delivered to our callback */
    assert(response.read_state == response::READ_STATUS ||
           response.read_state == response::READ_HEADERS);

    if (request.istream != nullptr)
        istream_close_handler(request.istream);

    Release(false);
}

static void
http_client_request_abort(struct async_operation *ao)
{
    struct http_client &client = http_client::FromAsync(*ao);

    client.Abort();
}

static const struct async_operation_class http_client_async_operation = {
    .abort = http_client_request_abort,
};


/*
 * constructor
 *
 */

void
http_client_request(struct pool *caller_pool,
                    int fd, enum istream_direct fd_type,
                    const struct lease *lease, void *lease_ctx,
                    const SocketFilter *filter, void *filter_ctx,
                    http_method_t method, const char *uri,
                    const struct growing_buffer *headers,
                    struct istream *body, bool expect_100,
                    const struct http_response_handler *handler,
                    void *ctx,
                    struct async_operation_ref *async_ref)
{
    assert(fd >= 0);
    assert(http_method_is_valid(method));
    assert(handler != nullptr);
    assert(handler->response != nullptr);

    if (!uri_path_verify_quick(uri)) {
        lease_direct_release(lease, lease_ctx, true);
        if (body != nullptr)
            istream_close_unused(body);

        GError *error = g_error_new(http_client_quark(),
                                    HTTP_CLIENT_UNSPECIFIED,
                                    "malformed request URI '%s'", uri);
        http_response_handler_direct_abort(handler, ctx, error);
        return;
    }

    struct pool *pool =
        pool_new_linear(caller_pool, "http_client_request", 8192);

    auto client = NewFromPool<struct http_client>(*pool);
    client->stopwatch = stopwatch_fd_new(pool, fd, uri);
    client->pool = pool;
    client->peer_name = p_strdup(pool, get_peer_name(fd));

    client->socket.Init(*pool, fd, fd_type,
                        &http_client_timeout, &http_client_timeout,
                        filter, filter_ctx,
                        http_client_socket_handler, client);
    p_lease_ref_set(&client->lease_ref, lease, lease_ctx,
                    pool, "http_client_lease");

    client->response.read_state = http_client::response::READ_STATUS;
    client->response.no_body = http_method_is_empty(method);

    pool_ref(caller_pool);
    client->caller_pool = caller_pool;
    http_response_handler_set(&client->request.handler, handler, ctx);

    client->request.async.Init(http_client_async_operation);
    async_ref->Set(client->request.async);

    /* request line */

    const char *p = p_strcat(client->pool,
                             http_method_to_string(method), " ", uri,
                             " HTTP/1.1\r\n", nullptr);
    struct istream *request_line_stream = istream_string_new(client->pool, p);

    /* headers */

    struct istream *header_stream = headers != nullptr
        ? istream_gb_new(client->pool, headers)
        : istream_null_new(client->pool);

    struct growing_buffer *headers2 =
        growing_buffer_new(client->pool, 256);

    if (body != nullptr) {
        off_t content_length = istream_available(body, false);
        if (content_length == (off_t)-1) {
            header_write(headers2, "transfer-encoding", "chunked");
            body = istream_chunked_new(client->pool, body);
        } else {
            snprintf(client->request.content_length_buffer,
                     sizeof(client->request.content_length_buffer),
                     "%lu", (unsigned long)content_length);
            header_write(headers2, "content-length",
                         client->request.content_length_buffer);
        }

        off_t available = expect_100 ? istream_available(body, true) : 0;
        if (available < 0 || available >= EXPECT_100_THRESHOLD) {
            /* large request body: ask the server for confirmation
               that he's really interested */
            header_write(headers2, "expect", "100-continue");
            body = client->request.body = istream_optional_new(pool, body);
        } else
            /* short request body: send it immediately */
            client->request.body = nullptr;
    } else
        client->request.body = nullptr;

    growing_buffer_write_buffer(headers2, "\r\n", 2);

    struct istream *header_stream2 = istream_gb_new(client->pool, headers2);

    /* request istream */

    client->request.istream = istream_cat_new(client->pool,
                                              request_line_stream,
                                              header_stream, header_stream2,
                                              body,
                                              nullptr);

    istream_handler_set(client->request.istream,
                        &http_client_request_stream_handler, client,
                        client->socket.GetDirectMask());

    client->socket.ScheduleReadNoTimeout(true);
    istream_read(client->request.istream);
}
