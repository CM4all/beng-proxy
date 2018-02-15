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

#include "Internal.hxx"
#include "Handler.hxx"
#include "Request.hxx"
#include "strmap.hxx"
#include "address_string.hxx"
#include "pool/pool.hxx"
#include "paddress.hxx"
#include "istream/Bucket.hxx"
#include "system/Error.hxx"
#include "util/StringView.hxx"
#include "util/StaticArray.hxx"
#include "util/RuntimeError.hxx"

#include <assert.h>
#include <unistd.h>

const struct timeval http_server_idle_timeout = {
    .tv_sec = 30,
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
                                          connection->remote_host,
                                          method, uri);
}

HttpServerConnection::BucketResult
HttpServerConnection::TryWriteBuckets2()
{
    assert(IsValid());
    assert(request.read_state != Request::START &&
           request.read_state != Request::HEADERS);
    assert(request.request != nullptr);
    assert(response.istream.IsDefined());

    if (socket.HasFilter())
        return BucketResult::MORE;

    IstreamBucketList list;

    try {
        response.istream.FillBucketList(list);
    } catch (...) {
        response.istream.Clear();
        std::throw_with_nested(std::runtime_error("error on HTTP response stream"));
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

        SocketErrorErrno("write error on HTTP connection");
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
    BucketResult result;

    try {
        result = TryWriteBuckets2();
    } catch (...) {
        assert(!response.istream.IsDefined());

        /* we clear this CancellablePointer here so CloseRequest()
           won't think we havn't sent a response yet */
        request.cancel_ptr = nullptr;

        Error(std::current_exception());
        return BucketResult::DESTROYED;
    }

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

    case BucketResult::DESTROYED:
        return false;
    }

    const DestructObserver destructed(*this);
    response.istream.Read();
    return !destructed;
}

/*
 * buffered_socket handler
 *
 */

BufferedResult
HttpServerConnection::OnBufferedData(const void *data, size_t length)
{
    if (response.pending_drained) {
        /* discard all incoming data while we're waiting for the
           (filtered) response to be drained */
        socket.Consumed(length);
        return BufferedResult::OK;
    }

    return Feed(data, length);
}

DirectResult
HttpServerConnection::OnBufferedDirect(SocketDescriptor fd, FdType fd_type)
{
    assert(request.read_state != Request::END);
    assert(!response.pending_drained);

    return TryRequestBodyDirect(fd, fd_type);
}

bool
HttpServerConnection::OnBufferedWrite()
{
    assert(!response.pending_drained);

    response.want_write = false;

    if (!TryWrite())
        return false;

    if (!response.want_write)
        socket.UnscheduleWrite();

    return true;
}

bool
HttpServerConnection::OnBufferedDrained() noexcept
{
    if (response.pending_drained) {
        Done();
        return false;
    }

    return true;
}

bool
HttpServerConnection::OnBufferedClosed() noexcept
{
    Cancel();
    return false;
}

void
HttpServerConnection::OnBufferedError(std::exception_ptr ep) noexcept
{
    SocketError(ep);
}

inline void
HttpServerConnection::IdleTimeoutCallback()
{
    assert(request.read_state == Request::START ||
           request.read_state == Request::HEADERS);

    Cancel();
}

inline
HttpServerConnection::HttpServerConnection(struct pool &_pool,
                                           EventLoop &_loop,
                                           SocketDescriptor fd, FdType fd_type,
                                           SocketFilterPtr &&filter,
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
     remote_host(address_to_host_string(*pool, _remote_address)),
     date_header(_date_header)
{
    pool_ref(pool);

    socket.Init(fd, fd_type,
                nullptr, &http_server_write_timeout,
                std::move(filter),
                *this);

    idle_timeout.Add(http_server_idle_timeout);

    /* read the first request, but not in this stack frame, because a
       failure may destroy the HttpServerConnection before it gets
       passed to the caller */
    defer_read.Schedule();
}

HttpServerConnection *
http_server_connection_new(struct pool *pool,
                           EventLoop &loop,
                           SocketDescriptor fd, FdType fd_type,
                           SocketFilterPtr filter,
                           SocketAddress local_address,
                           SocketAddress remote_address,
                           bool date_header,
                           HttpServerConnectionHandler &handler)
{
    assert(fd.IsDefined());

    return NewFromPool<HttpServerConnection>(*pool, *pool, loop, fd, fd_type,
                                             std::move(filter),
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

    DeleteUnrefTrashPool(request.request->pool, request.request);
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
HttpServerConnection::Error(std::exception_ptr e)
{
    assert(handler != nullptr);

    DestroySocket();

    const ScopePoolRef ref(*pool TRACE_ARGS);

    if (request.read_state != Request::START)
        CloseRequest();

    if (handler != nullptr) {
        auto *_handler = handler;
        handler = nullptr;
        _handler->HttpConnectionError(e);
    }

    Delete();
}

void
HttpServerConnection::Error(const char *msg)
{
    Error(std::make_exception_ptr(std::runtime_error(msg)));
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
HttpServerConnection::SocketErrorErrno(const char *msg)
{
    if (errno == EPIPE || errno == ECONNRESET) {
        /* don't report this common problem */
        Cancel();
        return;
    }

    try {
        throw MakeErrno(msg);
    } catch (...) {
        Error(std::make_exception_ptr(HttpServerSocketError()));
    }
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
