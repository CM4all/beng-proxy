/*
 * FastCGI client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Client.hxx"
#include "Quark.hxx"
#include "Protocol.hxx"
#include "Serialize.hxx"
#include "buffered_socket.hxx"
#include "growing_buffer.hxx"
#include "http_response.hxx"
#include "async.hxx"
#include "istream_fcgi.hxx"
#include "istream_gb.hxx"
#include "istream/istream_oo.hxx"
#include "istream/istream_internal.hxx"
#include "istream/istream_cat.hxx"
#include "please.hxx"
#include "header_parser.hxx"
#include "pevent.hxx"
#include "direct.hxx"
#include "fd-util.h"
#include "strmap.hxx"
#include "product.h"
#include "pool.hxx"
#include "util/Cast.hxx"
#include "util/ConstBuffer.hxx"
#include "util/CharUtil.hxx"
#include "util/ByteOrder.hxx"

#include <glib.h>

#include <sys/socket.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#ifndef NDEBUG
static LIST_HEAD(fcgi_clients);
#endif

struct FcgiClient {
#ifndef NDEBUG
    struct list_head siblings;
#endif

    struct pool &pool, &caller_pool;

    BufferedSocket socket;

    struct lease_ref lease_ref;

    const int stderr_fd;

    struct http_response_handler_ref handler;
    struct async_operation operation;

    const uint16_t id;

    struct {
        struct istream *istream;

        /**
         * This flag is set when the request istream has submitted
         * data.  It is used to check whether the request istream is
         * unavailable, to unschedule the socket write event.
         */
        bool got_data;
    } request;

    struct Response {
        enum {
            READ_HEADERS,

            /**
             * There is no response body.  Waiting for the
             * #FCGI_END_REQUEST packet, and then we'll forward the
             * response to the #http_response_handler.
             */
            READ_NO_BODY,

            READ_BODY,
        } read_state = READ_HEADERS;

        /**
         * Only used when read_state==READ_NO_BODY.
         */
        http_status_t status;

        struct strmap *const headers;

        off_t available;

        /**
         * This flag is true in HEAD requests.  HEAD responses may
         * contain a Content-Length header, but no response body will
         * follow (RFC 2616 4.3).
         */
        const bool no_body;

        /**
         * This flag is true if SubmitResponse() is currently calling
         * the HTTP response handler.  During this period,
         * fcgi_client_response_body_read() does nothing, to prevent
         * recursion.
         */
        bool in_handler;

        /**
         * Is the FastCGI application currently sending a STDERR
         * packet?
         */
        bool stderr;

        Response(struct pool &p, bool _no_body)
            :headers(strmap_new(&p)), no_body(_no_body) {}
    } response;

    struct istream response_body;

    size_t content_length = 0, skip_length = 0;

    FcgiClient(struct pool &_pool, struct pool &_caller_pool,
               int fd, FdType fd_type, Lease &lease,
               int _stderr_fd,
               uint16_t _id, http_method_t method,
               const struct http_response_handler &_handler, void *_ctx,
               struct async_operation_ref &async_ref);

    ~FcgiClient();

    void Abort();

    /**
     * Release the socket held by this object.
     */
    void ReleaseSocket(bool reuse) {
        socket.Abandon();
        p_lease_release(lease_ref, reuse, pool);
    }

    /**
     * Release resources held by this object: the event object, the
     * socket lease, and the pool reference.
     */
    void Release(bool reuse);

    /**
     * Abort receiving the response status/headers from the FastCGI
     * server, and notify the HTTP response handler.
     */
    void AbortResponseHeaders(GError *error);

    /**
     * Abort receiving the response body from the FastCGI server, and
     * notify the response body istream handler.
     */
    void AbortResponseBody(GError *error);

    /**
     * Abort receiving the response from the FastCGI server.  This is
     * a wrapper for AbortResponseHeaders() or AbortResponseBody().
     */
    void AbortResponse(GError *error);

    /**
     * Close the response body.  This is a request from the istream
     * client, and we must not call it back according to the istream API
     * definition.
     */
    void CloseResponseBody();

    /**
     * Find the #FCGI_END_REQUEST packet matching the current request, and
     * returns the offset where it ends, or 0 if none was found.
     */
    gcc_pure
    size_t FindEndRequest(const uint8_t *const data0, size_t size) const;

    bool HandleLine(const char *line, size_t length);

    size_t ParseHeaders(const char *data, size_t length);

    /**
     * Feed data into the FastCGI protocol parser.
     *
     * @return the number of bytes consumed, or 0 if this object has
     * been destructed
     */
    size_t Feed(const uint8_t *data, size_t length);

    void InitResponseBody();

    /**
     * Submit the response metadata to the #http_response_handler.
     *
     * @return false if the connection was closed
     */
    bool SubmitResponse();

    /* handler */
    size_t OnData(const void *data, size_t length);
    ssize_t OnDirect(FdType type, int fd, size_t max_length);
    void OnEof();
    void OnError(GError *error);
};

static constexpr struct timeval fcgi_client_timeout = {
    .tv_sec = 120,
    .tv_usec = 0,
};

inline FcgiClient::~FcgiClient()
{
    socket.Destroy();

    if (stderr_fd >= 0)
        close(stderr_fd);

#ifndef NDEBUG
    list_remove(&siblings);
#endif

    pool_unref(&caller_pool);
    pool_unref(&pool);
}

void
FcgiClient::Release(bool reuse)
{
    if (socket.IsConnected())
        ReleaseSocket(reuse);

    this->~FcgiClient();
}

void
FcgiClient::AbortResponseHeaders(GError *error)
{
    assert(response.read_state == Response::READ_HEADERS ||
           response.read_state == Response::READ_NO_BODY);

    operation.Finished();

    if (socket.IsConnected())
        ReleaseSocket(false);

    if (request.istream != nullptr)
        istream_free_handler(&request.istream);

    handler.InvokeAbort(error);

    Release(false);
}

void
FcgiClient::AbortResponseBody(GError *error)
{
    assert(response.read_state == Response::READ_BODY);

    if (socket.IsConnected())
        ReleaseSocket(false);

    if (request.istream != nullptr)
        istream_free_handler(&request.istream);

    istream_deinit_abort(&response_body, error);
    Release(false);
}

void
FcgiClient::AbortResponse(GError *error)
{
    assert(response.read_state == Response::READ_HEADERS ||
           response.read_state == Response::READ_NO_BODY ||
           response.read_state == Response::READ_BODY);

    if (response.read_state != Response::READ_BODY)
        AbortResponseHeaders(error);
    else
        AbortResponseBody(error);
}

inline void
FcgiClient::CloseResponseBody()
{
    assert(response.read_state == Response::READ_BODY);

    if (socket.IsConnected())
        ReleaseSocket(false);

    if (request.istream != nullptr)
        istream_free_handler(&request.istream);

    istream_deinit(&response_body);
    Release(false);
}

inline size_t
FcgiClient::FindEndRequest(const uint8_t *const data0, size_t size) const
{
    const uint8_t *data = data0, *const end = data0 + size;

    /* skip the rest of the current packet */
    data += content_length + skip_length;

    while (true) {
        const struct fcgi_record_header *header =
            (const struct fcgi_record_header *)data;
        data = (const uint8_t *)(header + 1);
        if (data > end)
            /* reached the end of the given buffer: not found */
            return 0;

        data += FromBE16(header->content_length);
        data += header->padding_length;

        if (header->request_id == id && header->type == FCGI_END_REQUEST)
            /* found it: return the packet end offset */
            return data - data0;
    }
}

inline bool
FcgiClient::HandleLine(const char *line, size_t length)
{
    assert(response.headers != nullptr);
    assert(line != nullptr);

    if (length > 0) {
        header_parse_line(&pool, response.headers, line, length);
        return false;
    } else {
        response.read_state = Response::READ_BODY;
        response.stderr = false;
        return true;
    }
}

inline size_t
FcgiClient::ParseHeaders(const char *data, size_t length)
{
    const char *p = data, *const data_end = data + length;

    const char *next = nullptr;
    bool finished = false;

    const char *eol;
    while ((eol = (const char *)memchr(p, '\n', data_end - p)) != nullptr) {
        next = eol + 1;
        --eol;
        while (eol >= p && IsWhitespaceOrNull(*eol))
            --eol;

        finished = HandleLine(p, eol - p + 1);
        if (finished)
            break;

        p = next;
    }

    return next != nullptr ? next - data : 0;
}

inline size_t
FcgiClient::Feed(const uint8_t *data, size_t length)
{
    if (response.stderr) {
        /* ignore errors and partial writes while forwarding STDERR
           payload; there's nothing useful we can do, and we can't let
           this delay/disturb the response delivery */
        if (stderr_fd >= 0)
            write(stderr_fd, data, length);
        else
            fwrite(data, 1, length, stderr);
        return length;
    }

    switch (response.read_state) {
        size_t consumed;

    case Response::READ_HEADERS:
        return ParseHeaders((const char *)data, length);

    case Response::READ_NO_BODY:
        /* unreachable */
        assert(false);
        return 0;

    case Response::READ_BODY:
        if (response.available == 0)
            /* discard following data */
            /* TODO: emit an error when that happens */
            return length;

        if (response.available > 0 &&
            (off_t)length > response.available)
            /* TODO: emit an error when that happens */
            length = response.available;

        consumed = istream_invoke_data(&response_body, data, length);
        if (consumed > 0 && response.available >= 0) {
            assert((off_t)consumed <= response.available);
            response.available -= consumed;
        }

        return consumed;
    }

    /* unreachable */
    assert(false);
    return 0;
}

inline bool
FcgiClient::SubmitResponse()
{
    assert(response.read_state == Response::READ_BODY);

    http_status_t status = HTTP_STATUS_OK;

    const char *p = response.headers->Remove("status");
    if (p != nullptr) {
        int i = atoi(p);
        if (http_status_is_valid((http_status_t)i))
            status = (http_status_t)i;
    }

    if (http_status_is_empty(status) || response.no_body) {
        response.read_state = Response::READ_NO_BODY;
        response.status = status;

        /* ignore the rest of this STDOUT payload */
        skip_length += content_length;
        content_length = 0;
        return true;
    }

    response.available = -1;
    p = response.headers->Remove("content-length");
    if (p != nullptr) {
        char *endptr;
        unsigned long long l = strtoull(p, &endptr, 10);
        if (endptr > p && *endptr == 0)
            response.available = l;
    }

    operation.Finished();

    InitResponseBody();
    struct istream *body = &response_body;

    const ScopePoolRef ref(caller_pool TRACE_ARGS);

    response.in_handler = true;
    handler.InvokeResponse(status, response.headers, body);
    response.in_handler = false;

    return socket.IsValid();
}

/**
 * Handle an END_REQUEST packet.  This function will always destroy
 * the client.
 */
static void
fcgi_client_handle_end(FcgiClient *client)
{
    assert(!client->socket.IsConnected());

    if (client->response.read_state == FcgiClient::Response::READ_HEADERS) {
        GError *error =
            g_error_new_literal(fcgi_quark(), 0,
                                "premature end of headers "
                                "from FastCGI application");
        client->AbortResponseHeaders(error);
        return;
    }

    if (client->request.istream != nullptr)
        istream_close_handler(client->request.istream);

    if (client->response.read_state == FcgiClient::Response::READ_NO_BODY) {
        client->operation.Finished();
        client->handler.InvokeResponse(client->response.status,
                                       client->response.headers,
                                       nullptr);
    } else if (client->response.available > 0) {
        GError *error =
            g_error_new_literal(fcgi_quark(), 0,
                                "premature end of body "
                                "from FastCGI application");
        client->AbortResponseBody(error);
        return;
    } else
        istream_deinit_eof(&client->response_body);

    client->Release(false);
}

/**
 * A packet header was received.
 *
 * @return false if the client has been destroyed
 */
static bool
fcgi_client_handle_header(FcgiClient *client,
                          const struct fcgi_record_header *header)
{
    client->content_length = FromBE16(header->content_length);
    client->skip_length = header->padding_length;

    if (header->request_id != client->id) {
        /* wrong request id; discard this packet */
        client->skip_length += client->content_length;
        client->content_length = 0;
        return true;
    }

    switch (header->type) {
    case FCGI_STDOUT:
        client->response.stderr = false;

        if (client->response.read_state == FcgiClient::Response::READ_NO_BODY) {
            /* ignore all payloads until #FCGI_END_REQUEST */
            client->skip_length += client->content_length;
            client->content_length = 0;
        }

        return true;

    case FCGI_STDERR:
        client->response.stderr = true;
        return true;

    case FCGI_END_REQUEST:
        fcgi_client_handle_end(client);
        return false;

    default:
        client->skip_length += client->content_length;
        client->content_length = 0;
        return true;
    }
}

/**
 * Consume data from the input buffer.
 */
static BufferedResult
fcgi_client_consume_input(FcgiClient *client,
                          const uint8_t *data0, size_t length0)
{
    const uint8_t *data = data0, *const end = data0 + length0;

    do {
        if (client->content_length > 0) {
            bool at_headers = client->response.read_state == FcgiClient::Response::READ_HEADERS;

            size_t length = end - data;
            if (length > client->content_length)
                length = client->content_length;

            size_t nbytes = client->Feed(data, length);
            if (nbytes == 0) {
                if (at_headers) {
                    /* incomplete header line received, want more
                       data */
                    assert(client->response.read_state == FcgiClient::Response::READ_HEADERS);
                    assert(client->socket.IsValid());
                    return BufferedResult::MORE;
                }

                if (!client->socket.IsValid())
                    return BufferedResult::CLOSED;

                /* the response body handler blocks, wait for it to
                   become ready */
                return BufferedResult::BLOCKING;
            }

            data += nbytes;
            client->content_length -= nbytes;
            client->socket.Consumed(nbytes);

            if (at_headers && client->response.read_state == FcgiClient::Response::READ_BODY) {
                /* the read_state has been switched from HEADERS to
                   BODY: we have to deliver the response now */

                if (!client->SubmitResponse())
                    return BufferedResult::CLOSED;

                /* continue parsing the response body from the
                   buffer */
                continue;
            }

            if (client->content_length > 0)
                return data < end && client->response.read_state != FcgiClient::Response::READ_HEADERS
                    /* some was consumed, try again later */
                    ? BufferedResult::PARTIAL
                    /* all input was consumed, want more */
                    : BufferedResult::MORE;

            continue;
        }

        if (client->skip_length > 0) {
            size_t nbytes = end - data;
            if (nbytes > client->skip_length)
                nbytes = client->skip_length;

            data += nbytes;
            client->skip_length -= nbytes;
            client->socket.Consumed(nbytes);

            if (client->skip_length > 0)
                return BufferedResult::MORE;

            continue;
        }

        const struct fcgi_record_header *header =
            (const struct fcgi_record_header *)data;
        const size_t remaining = end - data;
        if (remaining < sizeof(*header))
            return BufferedResult::MORE;

        data += sizeof(*header);
        client->socket.Consumed(sizeof(*header));

        if (!fcgi_client_handle_header(client, header))
            return BufferedResult::CLOSED;
    } while (data != end);

    return BufferedResult::MORE;
}

/*
 * istream handler for the request
 *
 */

inline size_t
FcgiClient::OnData(const void *data, size_t length)
{
    assert(socket.IsConnected());
    assert(request.istream != nullptr);

    request.got_data = true;

    ssize_t nbytes = socket.Write(data, length);
    if (nbytes > 0)
        socket.ScheduleWrite();
    else if (gcc_likely(nbytes == WRITE_BLOCKING || nbytes == WRITE_DESTROYED))
        return 0;
    else if (nbytes < 0) {
        GError *error = g_error_new(fcgi_quark(), errno,
                                    "write to FastCGI application failed: %s",
                                    strerror(errno));
        AbortResponse(error);
        return 0;
    }

    return (size_t)nbytes;
}

inline ssize_t
FcgiClient::OnDirect(FdType type, int fd, size_t max_length)
{
    assert(socket.IsConnected());

    request.got_data = true;

    ssize_t nbytes = socket.WriteFrom(fd, type, max_length);
    if (likely(nbytes > 0))
        socket.ScheduleWrite();
    else if (nbytes == WRITE_BLOCKING)
        return ISTREAM_RESULT_BLOCKING;
    else if (nbytes == WRITE_DESTROYED)
        return ISTREAM_RESULT_CLOSED;
    else if (nbytes < 0 && errno == EAGAIN) {
        request.got_data = false;
        socket.UnscheduleWrite();
    }

    return nbytes;
}

inline void
FcgiClient::OnEof()
{
    assert(request.istream != nullptr);

    request.istream = nullptr;

    socket.UnscheduleWrite();
}

inline void
FcgiClient::OnError(GError *error)
{
    assert(request.istream != nullptr);

    request.istream = nullptr;

    g_prefix_error(&error, "FastCGI request stream failed: ");
    AbortResponse(error);
}

/*
 * istream implementation for the response body
 *
 */

static inline FcgiClient *
response_stream_to_client(struct istream *istream)
{
    return &ContainerCast2(*istream, &FcgiClient::response_body);
}

static off_t
fcgi_client_response_body_available(struct istream *istream, bool partial)
{
    FcgiClient *client = response_stream_to_client(istream);

    if (client->response.available >= 0)
        return client->response.available;

    if (!partial || client->response.stderr)
        return -1;

    return client->content_length;
}

static void
fcgi_client_response_body_read(struct istream *istream)
{
    FcgiClient *client = response_stream_to_client(istream);

    if (client->response.in_handler)
        /* avoid recursion; the http_response_handler caller will
           continue parsing the response if possible */
        return;

    client->socket.Read(true);
}

static void
fcgi_client_response_body_close(struct istream *istream)
{
    FcgiClient *client = response_stream_to_client(istream);

    client->CloseResponseBody();
}

static constexpr struct istream_class fcgi_client_response_body = {
    .available = fcgi_client_response_body_available,
    .read = fcgi_client_response_body_read,
    .close = fcgi_client_response_body_close,
};

inline void
FcgiClient::InitResponseBody()
{
    istream_init(&response_body, &fcgi_client_response_body, &pool);
}

/*
 * socket_wrapper handler
 *
 */

static BufferedResult
fcgi_client_socket_data(const void *buffer, size_t size, void *ctx)
{
    FcgiClient *client = (FcgiClient *)ctx;

    if (client->socket.IsConnected()) {
        /* check if the #FCGI_END_REQUEST packet can be found in the
           following data chunk */
        size_t offset = client->FindEndRequest((const uint8_t *)buffer, size);
        if (offset > 0)
            /* found it: we no longer need the socket, everything we
               need is already in the given buffer */
            client->ReleaseSocket(offset == size);
    }

    const ScopePoolRef ref(client->pool TRACE_ARGS);
    return fcgi_client_consume_input(client, (const uint8_t *)buffer, size);
}

static bool
fcgi_client_socket_closed(void *ctx)
{
    FcgiClient *client = (FcgiClient *)ctx;

    /* the rest of the response may already be in the input buffer */
    client->ReleaseSocket(false);
    return true;
}

static bool
fcgi_client_socket_remaining(gcc_unused size_t remaining, void *ctx)
{
    gcc_unused
    FcgiClient *client = (FcgiClient *)ctx;

    /* only READ_BODY could have blocked */
    assert(client->response.read_state == FcgiClient::Response::READ_BODY);

    /* the rest of the response may already be in the input buffer */
    return true;
}

static bool
fcgi_client_socket_write(void *ctx)
{
    FcgiClient *client = (FcgiClient *)ctx;

    const ScopePoolRef ref(client->pool TRACE_ARGS);

    client->request.got_data = false;
    istream_read(client->request.istream);

    const bool result = client->socket.IsValid();
    if (result && client->request.istream != nullptr) {
        if (client->request.got_data)
            client->socket.ScheduleWrite();
        else
            client->socket.UnscheduleWrite();
    }

    return result;
}

static bool
fcgi_client_socket_timeout(void *ctx)
{
    FcgiClient *client = (FcgiClient *)ctx;

    GError *error = g_error_new_literal(fcgi_quark(), 0, "timeout");
    client->AbortResponse(error);
    return false;
}

static void
fcgi_client_socket_error(GError *error, void *ctx)
{
    FcgiClient *client = (FcgiClient *)ctx;

    client->AbortResponse(error);
}

static constexpr BufferedSocketHandler fcgi_client_socket_handler = {
    .data = fcgi_client_socket_data,
    .closed = fcgi_client_socket_closed,
    .remaining = fcgi_client_socket_remaining,
    .write = fcgi_client_socket_write,
    .timeout = fcgi_client_socket_timeout,
    .error = fcgi_client_socket_error,
};

/*
 * async operation
 *
 */

void
FcgiClient::Abort()
{
    /* async_operation_ref::Abort() can only be used before the
       response was delivered to our callback */
    assert(response.read_state == Response::READ_HEADERS ||
           response.read_state == Response::READ_NO_BODY);

    if (request.istream != nullptr)
        istream_close_handler(request.istream);

    Release(false);
}

/*
 * constructor
 *
 */

inline
FcgiClient::FcgiClient(struct pool &_pool, struct pool &_caller_pool,
                       int fd, FdType fd_type, Lease &lease,
                       int _stderr_fd,
                       uint16_t _id, http_method_t method,
                       const struct http_response_handler &_handler,
                       void *_ctx,
                       struct async_operation_ref &async_ref)
    :pool(_pool), caller_pool(_caller_pool),
     stderr_fd(_stderr_fd),
     id(_id),
     response(caller_pool, http_method_is_empty(method))
{
#ifndef NDEBUG
    list_add(&siblings, &fcgi_clients);
#endif
    pool_ref(&caller_pool);

    socket.Init(pool, fd, fd_type,
                &fcgi_client_timeout, &fcgi_client_timeout,
                fcgi_client_socket_handler, this);

    p_lease_ref_set(lease_ref, lease, pool, "fcgi_client_lease");

    handler.Set(_handler, _ctx);

    operation.Init2<FcgiClient>();
    async_ref.Set(operation);
}

void
fcgi_client_request(struct pool *caller_pool, int fd, FdType fd_type,
                    Lease &lease,
                    http_method_t method, const char *uri,
                    const char *script_filename,
                    const char *script_name, const char *path_info,
                    const char *query_string,
                    const char *document_root,
                    const char *remote_addr,
                    struct strmap *headers, struct istream *body,
                    ConstBuffer<const char *> params,
                    int stderr_fd,
                    const struct http_response_handler *handler,
                    void *handler_ctx,
                    struct async_operation_ref *async_ref)
{
    static unsigned next_request_id = 1;
    ++next_request_id;

    struct fcgi_record_header header = {
        .version = FCGI_VERSION_1,
        .request_id = GUINT16_TO_BE(next_request_id),
        .padding_length = 0,
        .reserved = 0,
    };
    static constexpr struct fcgi_begin_request begin_request = {
        .role = ToBE16(FCGI_RESPONDER),
        .flags = FCGI_KEEP_CONN,
    };

    assert(http_method_is_valid(method));

    struct pool *pool = pool_new_linear(caller_pool, "fcgi_client_request",
                                        2048);
    auto client = NewFromPool<FcgiClient>(*pool, *pool, *caller_pool,
                                          fd, fd_type, lease,
                                          stderr_fd,
                                          header.request_id, method,
                                          *handler, handler_ctx, *async_ref);

    GrowingBuffer *buffer = growing_buffer_new(pool, 1024);
    header.type = FCGI_BEGIN_REQUEST;
    header.content_length = ToBE16(sizeof(begin_request));
    growing_buffer_write_buffer(buffer, &header, sizeof(header));
    growing_buffer_write_buffer(buffer, &begin_request, sizeof(begin_request));

    fcgi_serialize_params(buffer, header.request_id,
                          "REQUEST_METHOD", http_method_to_string(method),
                          "REQUEST_URI", uri,
                          "SCRIPT_FILENAME", script_filename,
                          "SCRIPT_NAME", script_name,
                          "PATH_INFO", path_info,
                          "QUERY_STRING", query_string,
                          "DOCUMENT_ROOT", document_root,
                          "SERVER_SOFTWARE", PRODUCT_TOKEN,
                          nullptr);

    if (remote_addr != nullptr)
        fcgi_serialize_params(buffer, header.request_id,
                              "REMOTE_ADDR", remote_addr,
                              nullptr);

    off_t available = body != nullptr
        ? istream_available(body, false)
        : -1;
    if (available >= 0) {
        char value[64];
        snprintf(value, sizeof(value),
                 "%lu", (unsigned long)available);

        const char *content_type = strmap_get_checked(headers, "content-type");

        fcgi_serialize_params(buffer, header.request_id,
                              "HTTP_CONTENT_LENGTH", value,
                              /* PHP wants the parameter without
                                 "HTTP_" */
                              "CONTENT_LENGTH", value,
                              /* same for the "Content-Type" request
                                 header */
                              content_type != nullptr ? "CONTENT_TYPE" : nullptr,
                              content_type,
                              nullptr);
    }

    if (headers != nullptr)
        fcgi_serialize_headers(buffer, header.request_id, headers);

    if (!params.IsEmpty())
        fcgi_serialize_vparams(buffer, header.request_id, params);

    header.type = FCGI_PARAMS;
    header.content_length = ToBE16(0);
    growing_buffer_write_buffer(buffer, &header, sizeof(header));

    struct istream *request;

    if (body != nullptr)
        /* format the request body */
        request = istream_cat_new(pool,
                                  istream_gb_new(pool, buffer),
                                  istream_fcgi_new(pool, body,
                                                   header.request_id),
                                  nullptr);
    else {
        /* no request body - append an empty STDIN packet */
        header.type = FCGI_STDIN;
        header.content_length = ToBE16(0);
        growing_buffer_write_buffer(buffer, &header, sizeof(header));

        request = istream_gb_new(pool, buffer);
    }

    istream_assign_handler(&client->request.istream, request,
                           &MakeIstreamHandler<FcgiClient>::handler, client,
                           client->socket.GetDirectMask());

    client->socket.ScheduleReadNoTimeout(true);
    istream_read(client->request.istream);
}
