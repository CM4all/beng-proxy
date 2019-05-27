/*
 * Copyright 2007-2019 Content Management AG
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

#include "Client.hxx"
#include "Handler.hxx"
#include "Packet.hxx"
#include "Error.hxx"
#include "please.hxx"
#include "istream/Pointer.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/Handler.hxx"
#include "istream/Result.hxx"
#include "pool/pool.hxx"
#include "system/Error.hxx"
#include "event/net/BufferedSocket.hxx"
#include "util/Cancellable.hxx"
#include "util/Cast.hxx"
#include "util/ByteOrder.hxx"
#include "util/DestructObserver.hxx"
#include "util/Exception.hxx"

#include <errno.h>
#include <sys/socket.h>
#include <string.h>

struct MemcachedClient final
    : BufferedSocketHandler, Istream, IstreamHandler, Cancellable, DestructAnchor {

    enum class ReadState {
        HEADER,
        EXTRAS,
        KEY,
        VALUE,
        END,
    };

    /* I/O */
    BufferedSocket socket;
    struct lease_ref lease_ref;

    /* request */
    struct Request {
        MemcachedResponseHandler &handler;

        IstreamPointer istream;

        Request(UnusedIstreamPtr _istream,
                IstreamHandler &i_handler,
                MemcachedResponseHandler &_handler) noexcept
            :handler(_handler),
             istream(std::move(_istream), i_handler) {}

    } request;

    /* response */
    struct {
        ReadState read_state;

        /**
         * This flag is true if we are currently calling the
         * #memcached_client_handler.  During this period,
         * memcached_client_socket_read() does nothing, to prevent
         * recursion.
         */
        bool in_handler;

        struct memcached_response_header header;

        unsigned char *extras;

        struct {
            void *buffer;
            unsigned char *tail;
            size_t remaining;
        } key;

        /**
         * Total number of bytes remaining to read from the response,
         * including extras and key.
         */
        size_t remaining;
    } response;

    MemcachedClient(struct pool &_pool, EventLoop &event_loop,
                    SocketDescriptor fd, FdType fd_type,
                    Lease &lease,
                    UnusedIstreamPtr _request,
                    MemcachedResponseHandler &handler,
                    CancellablePointer &cancel_ptr);

    using Istream::GetPool;

    bool CheckDirect() const {
        assert(socket.IsConnected());
        assert(response.read_state == ReadState::VALUE);

        return Istream::CheckDirect(socket.GetType());
    }

    void ScheduleWrite() {
        socket.ScheduleWrite();
    }

    /**
     * Release the socket held by this object.
     */
    void ReleaseSocket(bool reuse) noexcept {
        socket.Abandon();
        p_lease_release(lease_ref, reuse, GetPool());
    }

    void DestroySocket(bool reuse) noexcept {
        if (socket.IsConnected())
            ReleaseSocket(reuse);
        socket.Destroy();
    }

    /**
     * Release resources held by this object: the event object, the
     * socket lease, and the pool reference.
     */
    void Release(bool reuse) noexcept {
        if (socket.IsValid())
            DestroySocket(reuse);

        Destroy();
    }

    void AbortResponseHeaders(std::exception_ptr ep);
    void AbortResponseValue(std::exception_ptr ep);
    void AbortResponse(std::exception_ptr ep);

    BufferedResult SubmitResponse();
    BufferedResult BeginKey();
    BufferedResult FeedHeader(const void *data, size_t length);
    BufferedResult FeedExtras(const void *data, size_t length);
    BufferedResult FeedKey(const void *data, size_t length);
    BufferedResult FeedValue(const void *data, size_t length);
    BufferedResult Feed(const void *data, size_t length);

    DirectResult TryReadDirect(SocketDescriptor fd, FdType type);

    /* virtual methods from class BufferedSocketHandler */
    BufferedResult OnBufferedData() override;
    DirectResult OnBufferedDirect(SocketDescriptor fd, FdType fd_type) override;
    bool OnBufferedClosed() noexcept override;
    bool OnBufferedRemaining(size_t remaining) noexcept override;
    bool OnBufferedWrite() override;
    void OnBufferedError(std::exception_ptr e) noexcept override;

    /* virtual methods from class Cancellable */
    void Cancel() noexcept override;

    /* virtual methods from class Istream */

    off_t _GetAvailable(bool partial) noexcept override;
    void _Read() noexcept override;
    void _Close() noexcept override;

    /* virtual methods from class IstreamHandler */
    size_t OnData(const void *data, size_t length) noexcept override;
    void OnEof() noexcept override;
    void OnError(std::exception_ptr ep) noexcept override;
};

static constexpr auto memcached_client_timeout = std::chrono::seconds(5);

void
MemcachedClient::AbortResponseHeaders(std::exception_ptr ep)
{
    assert(response.read_state == ReadState::HEADER ||
           response.read_state == ReadState::EXTRAS ||
           response.read_state == ReadState::KEY);

    if (socket.IsValid())
        DestroySocket(false);

    request.handler.OnMemcachedError(ep);
    response.read_state = ReadState::END;

    if (request.istream.IsDefined())
        request.istream.ClearAndClose();

    Destroy();
}

void
MemcachedClient::AbortResponseValue(std::exception_ptr ep)
{
    assert(response.read_state == ReadState::VALUE);
    assert(!request.istream.IsDefined());

    if (socket.IsValid())
        DestroySocket(false);

    response.read_state = ReadState::END;
    DestroyError(ep);
}

void
MemcachedClient::AbortResponse(std::exception_ptr ep)
{
    assert(response.read_state != ReadState::END);

    switch (response.read_state) {
    case ReadState::HEADER:
    case ReadState::EXTRAS:
    case ReadState::KEY:
        AbortResponseHeaders(ep);
        return;

    case ReadState::VALUE:
        AbortResponseValue(ep);
        return;

    case ReadState::END:
        gcc_unreachable();
    }

    gcc_unreachable();
}

/*
 * response value istream
 *
 */

off_t
MemcachedClient::_GetAvailable(gcc_unused bool partial) noexcept
{
    assert(response.read_state == ReadState::VALUE);
    assert(!request.istream.IsDefined());

    return response.remaining;
}

void
MemcachedClient::_Read() noexcept
{
    assert(response.read_state == ReadState::VALUE);
    assert(!request.istream.IsDefined());

    if (response.in_handler)
        /* avoid recursion; the memcached_client_handler caller will
           continue parsing the response if possible */
        return;

    if (socket.IsConnected())
        socket.SetDirect(CheckDirect());

    socket.Read(true);
}

void
MemcachedClient::_Close() noexcept
{
    assert(response.read_state == ReadState::VALUE);
    assert(!request.istream.IsDefined());

    Release(false);
}

/*
 * response parser
 *
 */

BufferedResult
MemcachedClient::SubmitResponse()
{
    assert(response.read_state == ReadState::KEY);

    if (request.istream.IsDefined()) {
        /* at this point, the request must have been sent */
        AbortResponseHeaders(std::make_exception_ptr(MemcachedClientError("memcached server sends response too early")));
        return BufferedResult::CLOSED;
    }

    if (response.remaining > 0) {
        /* there's a value: pass it to the callback, continue
           reading */

        response.read_state = ReadState::VALUE;

        const DestructObserver destructed(*this);

        response.in_handler = true;
        request.handler.OnMemcachedResponse((memcached_response_status)FromBE16(response.header.status),
                                            response.extras,
                                            response.header.extras_length,
                                            response.key.buffer,
                                            FromBE16(response.header.key_length),
                                            UnusedIstreamPtr(this));
        if (destructed)
            return BufferedResult::CLOSED;

        response.in_handler = false;

        if (socket.IsConnected())
            socket.SetDirect(CheckDirect());

        return BufferedResult::AGAIN_EXPECT;
    } else {
        /* no value: invoke the callback, quit */

        DestroySocket(socket.IsEmpty());

        response.read_state = ReadState::END;

        request.handler.OnMemcachedResponse((memcached_response_status)FromBE16(response.header.status),
                                            response.extras,
                                            response.header.extras_length,
                                            response.key.buffer,
                                            FromBE16(response.header.key_length),
                                            nullptr);

        Release(false);
        return BufferedResult::CLOSED;
    }
}

BufferedResult
MemcachedClient::BeginKey()
{
    assert(response.read_state == ReadState::EXTRAS);

    response.read_state = ReadState::KEY;

    response.key.remaining =
        FromBE16(response.header.key_length);
    if (response.key.remaining == 0) {
        response.key.buffer = nullptr;
        return SubmitResponse();
    }

    response.key.buffer
        = response.key.tail
        = (unsigned char *)p_malloc(&GetPool(),
                                    response.key.remaining);

    return BufferedResult::AGAIN_EXPECT;
}

BufferedResult
MemcachedClient::FeedHeader(const void *data, size_t length)
{
    assert(response.read_state == ReadState::HEADER);

    if (length < sizeof(response.header))
        /* not enough data yet */
        return BufferedResult::MORE;

    memcpy(&response.header, data, sizeof(response.header));
    socket.DisposeConsumed(sizeof(response.header));

    response.read_state = ReadState::EXTRAS;

    response.remaining = FromBE32(response.header.body_length);
    if (response.header.magic != MEMCACHED_MAGIC_RESPONSE ||
        FromBE16(response.header.key_length) +
        response.header.extras_length > response.remaining) {
        /* protocol error: abort the connection */
        AbortResponseHeaders(std::make_exception_ptr(MemcachedClientError("memcached protocol error")));
        return BufferedResult::CLOSED;
    }

    if (response.header.extras_length == 0) {
        response.extras = nullptr;
        return BeginKey();
    }

    return BufferedResult::AGAIN_EXPECT;
}

BufferedResult
MemcachedClient::FeedExtras(const void *data, size_t length)
{
    assert(response.read_state == ReadState::EXTRAS);
    assert(response.header.extras_length > 0);

    if (data == nullptr ||
        length < sizeof(response.header.extras_length))
        return BufferedResult::MORE;

    response.extras = (unsigned char *)
        p_malloc(&GetPool(), response.header.extras_length);
    memcpy(response.extras, data,
           response.header.extras_length);

    socket.DisposeConsumed(response.header.extras_length);
    response.remaining -= response.header.extras_length;

    return BeginKey();
}

BufferedResult
MemcachedClient::FeedKey(const void *data, size_t length)
{
    assert(response.read_state == ReadState::KEY);
    assert(response.key.remaining > 0);

    if (length > response.key.remaining)
        length = response.key.remaining;

    memcpy(response.key.tail, data, length);
    response.key.tail += length;
    response.key.remaining -= length;
    response.remaining -= FromBE16(response.header.key_length);

    socket.DisposeConsumed(length);

    if (response.key.remaining == 0)
        return SubmitResponse();

    return BufferedResult::MORE;
}

BufferedResult
MemcachedClient::FeedValue(const void *data, size_t length)
{
    assert(response.read_state == ReadState::VALUE);
    assert(response.remaining > 0);

    if (socket.IsConnected() &&
        length >= response.remaining)
        ReleaseSocket(length == response.remaining);

    if (length > response.remaining)
        length = response.remaining;

    const DestructObserver destructed(*this);

    size_t nbytes = InvokeData(data, length);
    if (nbytes == 0)
        return destructed
            ? BufferedResult::CLOSED
            : BufferedResult::BLOCKING;

    socket.DisposeConsumed(nbytes);

    response.remaining -= nbytes;
    if (response.remaining > 0)
        return nbytes < length
            ? BufferedResult::OK
            : BufferedResult::MORE;

    assert(!socket.IsConnected());
    assert(!request.istream.IsDefined());

    response.read_state = ReadState::END;
    InvokeEof();

    Release(false);
    return BufferedResult::CLOSED;
}

BufferedResult
MemcachedClient::Feed(const void *data, size_t length)
{
    switch (response.read_state) {
    case ReadState::HEADER:
        return FeedHeader(data, length);

    case ReadState::EXTRAS:
        return FeedExtras(data, length);

    case ReadState::KEY:
        return FeedKey(data, length);

    case ReadState::VALUE:
        return FeedValue(data, length);

    case ReadState::END:
        gcc_unreachable();
    }

    gcc_unreachable();
}

DirectResult
MemcachedClient::TryReadDirect(SocketDescriptor fd, FdType type)
{
    assert(response.read_state == ReadState::VALUE);
    assert(response.remaining > 0);

    ssize_t nbytes = InvokeDirect(type, fd.Get(), response.remaining);
    if (gcc_likely(nbytes > 0)) {
        response.remaining -= nbytes;

        if (response.remaining == 0) {
            DestroySocket(true);
            DestroyEof();
            return DirectResult::CLOSED;
        } else
            return DirectResult::OK;
    } else if (gcc_unlikely(nbytes == ISTREAM_RESULT_EOF)) {
        return DirectResult::END;
    } else if (nbytes == ISTREAM_RESULT_BLOCKING) {
        return DirectResult::BLOCKING;
    } else if (nbytes == ISTREAM_RESULT_CLOSED) {
        return DirectResult::CLOSED;
    } else if (errno == EAGAIN) {
        return DirectResult::EMPTY;
    } else {
        return DirectResult::ERRNO;
    }
}

/*
 * socket_wrapper handler
 *
 */

bool
MemcachedClient::OnBufferedWrite()
{
    assert(response.read_state != ReadState::END);

    const DestructObserver destructed(*this);

    request.istream.Read();

    return !destructed && socket.IsConnected();
}

BufferedResult
MemcachedClient::OnBufferedData()
{
    assert(response.read_state != ReadState::END);

    const auto r = socket.ReadBuffer();
    assert(!r.empty());

    return Feed(r.data, r.size);
}

DirectResult
MemcachedClient::OnBufferedDirect(SocketDescriptor fd, FdType type)
{
    assert(response.read_state == ReadState::VALUE);
    assert(response.remaining > 0);
    assert(CheckDirect());

    return TryReadDirect(fd, type);
}

bool
MemcachedClient::OnBufferedClosed() noexcept
{
    /* the rest of the response may already be in the input buffer */
    ReleaseSocket(false);
    return true;
}

bool
MemcachedClient::OnBufferedRemaining(gcc_unused size_t remaining) noexcept
{
    /* only READ_VALUE could have blocked */
    assert(response.read_state == ReadState::VALUE);

    /* the rest of the response may already be in the input buffer */
    return true;
}

void
MemcachedClient::OnBufferedError(std::exception_ptr ep) noexcept
{
    AbortResponse(NestException(ep,
                                MemcachedClientError("memcached connection failed")));
}

/*
 * istream handler for the request
 *
 */

inline size_t
MemcachedClient::OnData(const void *data, size_t length) noexcept
{
    assert(request.istream.IsDefined());
    assert(response.read_state == ReadState::HEADER ||
           response.read_state == ReadState::EXTRAS ||
           response.read_state == ReadState::KEY);
    assert(data != nullptr);
    assert(length > 0);

    ssize_t nbytes = socket.Write(data, length);
    if (nbytes < 0) {
        if (nbytes == WRITE_BLOCKING || nbytes == WRITE_DESTROYED)
            return 0;

        AbortResponseHeaders(std::make_exception_ptr(MakeErrno("write error on memcached connection")));
        return 0;
    }

    ScheduleWrite();
    return (size_t)nbytes;
}

void
MemcachedClient::OnEof() noexcept
{
    assert(request.istream.IsDefined());
    assert(response.read_state == ReadState::HEADER ||
           response.read_state == ReadState::EXTRAS ||
           response.read_state == ReadState::KEY);

    request.istream.Clear();

    socket.UnscheduleWrite();
    socket.Read(true);
}

void
MemcachedClient::OnError(std::exception_ptr ep) noexcept
{
    assert(request.istream.IsDefined());
    assert(response.read_state == ReadState::HEADER ||
           response.read_state == ReadState::EXTRAS ||
           response.read_state == ReadState::KEY);

    request.istream.Clear();
    AbortResponse(ep);
}

/*
 * async operation
 *
 */

void
MemcachedClient::Cancel() noexcept
{
    IstreamPointer request_istream = std::move(request.istream);

    /* Cancellable::Cancel() can only be used before the
       response was delivered to our callback */
    assert(response.read_state == ReadState::HEADER ||
           response.read_state == ReadState::EXTRAS ||
           response.read_state == ReadState::KEY);

    Release(false);

    if (request_istream.IsDefined())
        request_istream.Close();
}

/*
 * constructor
 *
 */

inline
MemcachedClient::MemcachedClient(struct pool &_pool, EventLoop &event_loop,
                                 SocketDescriptor fd, FdType fd_type,
                                 Lease &lease,
                                 UnusedIstreamPtr _request,
                                 MemcachedResponseHandler &handler,
                                 CancellablePointer &cancel_ptr)
    :Istream(_pool),
     socket(event_loop),
     request(std::move(_request), *this, handler)
{
    socket.Init(fd, fd_type,
                Event::Duration(-1), memcached_client_timeout,
                *this);

    p_lease_ref_set(lease_ref, lease, GetPool(), "memcached_client_lease");

    cancel_ptr = *this;

    response.read_state = MemcachedClient::ReadState::HEADER;

    request.istream.Read();
}

void
memcached_client_invoke(struct pool *pool, EventLoop &event_loop,
                        SocketDescriptor fd, FdType fd_type,
                        Lease &lease,
                        enum memcached_opcode opcode,
                        const void *extras, size_t extras_length,
                        const void *key, size_t key_length,
                        UnusedIstreamPtr value,
                        MemcachedResponseHandler &handler,
                        CancellablePointer &cancel_ptr)
{
    assert(extras_length <= MEMCACHED_EXTRAS_MAX);
    assert(key_length <= MEMCACHED_KEY_MAX);

    auto request = memcached_request_packet(*pool, opcode,
                                            extras, extras_length,
                                            key, key_length, std::move(value),
                                            0x1234 /* XXX? */);
    if (!request) {
        lease.ReleaseLease(true);

        handler.OnMemcachedError(std::make_exception_ptr(MemcachedClientError("failed to generate memcached request packet")));
        return;
    }

    NewFromPool<MemcachedClient>(*pool, *pool, event_loop,
                                 fd, fd_type, lease,
                                 std::move(request),
                                 handler, cancel_ptr);
}
