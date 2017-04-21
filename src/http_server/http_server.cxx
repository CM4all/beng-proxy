/*
 * HTTP server implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Internal.hxx"
#include "Handler.hxx"
#include "Request.hxx"
#include "strmap.hxx"
#include "address_string.hxx"
#include "gerrno.h"
#include "pool.hxx"
#include "paddress.hxx"
#include "GException.hxx"
#include "istream/Bucket.hxx"
#include "util/StringView.hxx"
#include "util/StaticArray.hxx"

#include <inline/compiler.h>
#include <daemon/log.h>

#include <assert.h>
#include <unistd.h>

const struct timeval http_server_idle_timeout = {
    .tv_sec = 30,
    .tv_usec = 0,
};

const struct timeval http_server_header_timeout = {
    .tv_sec = 20,
    .tv_usec = 0,
};

const struct timeval http_server_read_timeout = {
    .tv_sec = 30,
    .tv_usec = 0,
};

const struct timeval http_server_write_timeout = {
    .tv_sec = 30,
    .tv_usec = 0,
};

void
HttpServerConnection::Log()
{
    if (handler == nullptr)
        /* this can happen when called via
           http_server_connection_close() (during daemon shutdown) */
        return;

    handler->LogHttpRequest(*request.request,
                            response.status,
                            response.length,
                            request.bytes_received,
                            response.bytes_sent);
}

HttpServerRequest *
http_server_request_new(HttpServerConnection *connection,
                        http_method_t method,
                        StringView uri)
{
    assert(connection != nullptr);

    connection->response.status = http_status_t(0);

    struct pool *pool = pool_new_linear(connection->pool,
                                        "http_server_request", 8192);
    pool_set_major(pool);

    return NewFromPool<HttpServerRequest>(*pool, *pool,
                                          *connection,
                                          connection->local_address,
                                          connection->remote_address,
                                          connection->local_host_and_port,
                                          connection->remote_host_and_port,
                                          connection->remote_host,
                                          method, uri);
}

HttpServerConnection::BucketResult
HttpServerConnection::TryWriteBuckets2(GError **error_r)
{
    assert(IsValid());
    assert(request.read_state != Request::START &&
           request.read_state != Request::HEADERS);
    assert(request.request != nullptr);
    assert(response.istream.IsDefined());

    if (socket.HasFilter())
        return BucketResult::MORE;

    IstreamBucketList list;
    if (!response.istream.FillBucketList(list, error_r)) {
        response.istream.Clear();

        g_prefix_error(error_r, "error on HTTP response stream: ");
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
        return list.HasMore()
            ? BucketResult::MORE
            : BucketResult::DEPLETED;
    }

    ssize_t nbytes = socket.WriteV(v.begin(), v.size());
    if (nbytes < 0) {
        if (gcc_likely(nbytes == WRITE_BLOCKING))
            return BucketResult::BLOCKING;

        if (nbytes == WRITE_DESTROYED)
            return BucketResult::DESTROYED;

        ErrorErrno("write error on HTTP connection");
        return BucketResult::DESTROYED;
    }

    response.bytes_sent += nbytes;
    response.length += nbytes;

    size_t consumed = response.istream.ConsumeBucketList(nbytes);
    assert(consumed == (size_t)nbytes);

    return list.IsDepleted(consumed)
        ? BucketResult::DEPLETED
        : BucketResult::MORE;
}

HttpServerConnection::BucketResult
HttpServerConnection::TryWriteBuckets()
{
    GError *error = nullptr;
    auto result = TryWriteBuckets2(&error);
    switch (result) {
    case BucketResult::MORE:
        assert(response.istream.IsDefined());
        break;

    case BucketResult::BLOCKING:
        assert(response.istream.IsDefined());
        response.want_write = true;
        ScheduleWrite();
        break;

    case BucketResult::DEPLETED:
        assert(response.istream.IsDefined());
        response.istream.ClearAndClose();
        if (!ResponseIstreamFinished())
            result = BucketResult::DESTROYED;
        break;

    case BucketResult::ERROR:
        assert(!response.istream.IsDefined());

        /* we clear this CancellablePointer here so CloseRequest()
           won't think we havn't sent a response yet */
        request.cancel_ptr = nullptr;

        Error(error);
        result = BucketResult::DESTROYED;
        break;

    case BucketResult::DESTROYED:
        break;
    }

    return result;
}

bool
HttpServerConnection::TryWrite()
{
    assert(IsValid());
    assert(request.read_state != Request::START &&
           request.read_state != Request::HEADERS);
    assert(request.request != nullptr);
    assert(response.istream.IsDefined());

    switch (TryWriteBuckets()) {
    case BucketResult::MORE:
        break;

    case BucketResult::BLOCKING:
    case BucketResult::DEPLETED:
        return true;

    case BucketResult::ERROR:
    case BucketResult::DESTROYED:
        return false;
    }

    const ScopePoolRef ref(*pool TRACE_ARGS);
    response.istream.Read();

    return IsValid();
}

/*
 * buffered_socket handler
 *
 */

static BufferedResult
http_server_socket_data(const void *data, size_t length, void *ctx)
{
    auto *connection = (HttpServerConnection *)ctx;

    if (connection->response.pending_drained) {
        /* discard all incoming data while we're waiting for the
           (filtered) response to be drained */
        connection->socket.Consumed(length);
        return BufferedResult::OK;
    }

    return connection->Feed(data, length);
}

static DirectResult
http_server_socket_direct(int fd, FdType fd_type, void *ctx)
{
    auto *connection = (HttpServerConnection *)ctx;

    assert(connection->request.read_state != HttpServerConnection::Request::END);
    assert(!connection->response.pending_drained);

    return connection->TryRequestBodyDirect(fd, fd_type);
}

static bool
http_server_socket_write(void *ctx)
{
    auto *connection = (HttpServerConnection *)ctx;

    assert(!connection->response.pending_drained);

    connection->response.want_write = false;

    if (!connection->TryWrite())
        return false;

    if (!connection->response.want_write)
        connection->socket.UnscheduleWrite();

    return true;
}

static bool
http_server_socket_drained(void *ctx)
{
    auto *connection = (HttpServerConnection *)ctx;

    if (connection->response.pending_drained) {
        connection->Done();
        return false;
    }

    return true;
}

static bool
http_server_socket_timeout(void *ctx)
{
    auto *connection = (HttpServerConnection *)ctx;

    daemon_log(4, "timeout on HTTP connection from '%s'\n",
               connection->remote_host_and_port);
    connection->Cancel();
    return false;
}

static bool
http_server_socket_closed(void *ctx)
{
    auto *connection = (HttpServerConnection *)ctx;

    connection->Cancel();
    return false;
}

static void
http_server_socket_error(std::exception_ptr ep, void *ctx)
{
    auto *connection = (HttpServerConnection *)ctx;

    connection->Error(ToGError(ep));
}

static constexpr BufferedSocketHandler http_server_socket_handler = {
    .data = http_server_socket_data,
    .direct = http_server_socket_direct,
    .closed = http_server_socket_closed,
    .remaining = nullptr,
    .end = nullptr,
    .write = http_server_socket_write,
    .drained = http_server_socket_drained,
    .timeout = http_server_socket_timeout,
    .broken = nullptr,
    .error = http_server_socket_error,
};

inline void
HttpServerConnection::IdleTimeoutCallback()
{
    daemon_log(4, "%s timeout on HTTP connection from '%s'\n",
               request.read_state == Request::START
               ? "idle"
               : (request.read_state == Request::HEADERS ? "header" : "read"),
               remote_host_and_port);
    Cancel();
}

inline
HttpServerConnection::HttpServerConnection(struct pool &_pool,
                                           EventLoop &_loop,
                                           int fd, FdType fd_type,
                                           const SocketFilter *filter,
                                           void *filter_ctx,
                                           SocketAddress _local_address,
                                           SocketAddress _remote_address,
                                           bool _date_header,
                                           HttpServerConnectionHandler &_handler)
    :pool(&_pool), socket(_loop),
     idle_timeout(_loop, BIND_THIS_METHOD(IdleTimeoutCallback)),
     defer_read(_loop, BIND_THIS_METHOD(OnDeferredRead)),
     handler(&_handler),
     local_address(DupAddress(*pool, _local_address)),
     remote_address(DupAddress(*pool, _remote_address)),
     local_host_and_port(address_to_string(*pool, _local_address)),
     remote_host_and_port(address_to_string(*pool, _remote_address)),
     remote_host(address_to_host_string(*pool, _remote_address)),
     date_header(_date_header)
{
    pool_ref(pool);

    socket.Init(fd, fd_type,
                nullptr, &http_server_write_timeout,
                filter, filter_ctx,
                http_server_socket_handler, this);

    idle_timeout.Add(http_server_idle_timeout);

    /* read the first request, but not in this stack frame, because a
       failure may destroy the HttpServerConnection before it gets
       passed to the caller */
    defer_read.Schedule();
}

HttpServerConnection *
http_server_connection_new(struct pool *pool,
                           EventLoop &loop,
                           int fd, FdType fd_type,
                           const SocketFilter *filter,
                           void *filter_ctx,
                           SocketAddress local_address,
                           SocketAddress remote_address,
                           bool date_header,
                           HttpServerConnectionHandler &handler)
{
    assert(fd >= 0);

    return NewFromPool<HttpServerConnection>(*pool, *pool, loop, fd, fd_type,
                                             filter, filter_ctx,
                                             local_address, remote_address,
                                             date_header,
                                             handler);
}

inline void
HttpServerConnection::CloseSocket()
{
    assert(socket.IsConnected());

    socket.Close();

    idle_timeout.Cancel();
}

void
HttpServerConnection::DestroySocket()
{
    assert(socket.IsValid());

    if (socket.IsConnected())
        CloseSocket();

    socket.Destroy();
}

void
HttpServerConnection::CloseRequest()
{
    assert(request.read_state != Request::START);
    assert(request.request != nullptr);

    if (response.status != http_status_t(0))
        Log();

    auto &request_pool = request.request->pool;
    pool_trash(&request_pool);
    pool_unref(&request_pool);
    request.request = nullptr;

    if ((request.read_state == Request::BODY ||
         request.read_state == Request::END)) {
        if (response.istream.IsDefined())
            response.istream.ClearAndClose();
        else if (request.cancel_ptr)
            /* don't call this if coming from
               _response_stream_abort() */
            request.cancel_ptr.Cancel();
    }

    /* the handler must have closed the request body */
    assert(request.read_state != Request::BODY);
}

void
HttpServerConnection::Done()
{
    assert(handler != nullptr);
    assert(request.read_state == Request::START);

    /* shut down the socket gracefully to allow the TCP stack to
       transfer remaining response data */
    socket.Shutdown();

    DestroySocket();

    auto *_handler = handler;
    handler = nullptr;

    _handler->HttpConnectionClosed();

    Delete();
}

void
HttpServerConnection::Cancel()
{
    assert(handler != nullptr);

    DestroySocket();

    const ScopePoolRef ref(*pool TRACE_ARGS);

    if (request.read_state != Request::START)
        CloseRequest();

    if (handler != nullptr) {
        handler->HttpConnectionClosed();
        handler = nullptr;
    }

    Delete();
}

void
HttpServerConnection::Error(GError *error)
{
    assert(handler != nullptr);

    DestroySocket();

    const ScopePoolRef ref(*pool TRACE_ARGS);

    if (request.read_state != Request::START)
        CloseRequest();

    if (handler != nullptr) {
        g_prefix_error(&error, "error on HTTP connection from '%s': ",
                       remote_host_and_port);

        auto *_handler = handler;
        handler = nullptr;
        _handler->HttpConnectionError(error);
    } else
        g_error_free(error);

    Delete();
}

void
HttpServerConnection::Error(const char *msg)
{
    GError *error = g_error_new_literal(http_server_quark(), 0, msg);
    Error(error);
}

void
http_server_connection_close(HttpServerConnection *connection)
{
    assert(connection != nullptr);

    connection->DestroySocket();

    connection->handler = nullptr;

    if (connection->request.read_state != HttpServerConnection::Request::START)
        connection->CloseRequest();

    connection->Delete();
}

void
HttpServerConnection::ErrorErrno(const char *msg)
{
    if (errno == EPIPE || errno == ECONNRESET) {
        /* don't report this common problem */
        Cancel();
        return;
    }

    GError *error = new_error_errno_msg(msg);
    Error(error);
}

void
http_server_connection_graceful(HttpServerConnection *connection)
{
    assert(connection != nullptr);

    if (connection->request.read_state == HttpServerConnection::Request::START)
        /* there is no request currently; close the connection
           immediately */
        connection->Done();
    else
        /* a request is currently being handled; disable keep_alive so
           the connection will be closed after this last request */
        connection->keep_alive = false;
}

enum http_server_score
http_server_connection_score(const HttpServerConnection *connection)
{
    return connection->score;
}
