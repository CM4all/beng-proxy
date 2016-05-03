/*
 * memcached client implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "memcached_client.hxx"
#include "memcached_packet.hxx"
#include "buffered_socket.hxx"
#include "please.hxx"
#include "async.hxx"
#include "istream/Pointer.hxx"
#include "pool.hxx"
#include "util/Cast.hxx"
#include "util/ByteOrder.hxx"

#include <daemon/log.h>

#include <glib.h>

#include <errno.h>
#include <sys/socket.h>
#include <string.h>

struct MemcachedClient final : Istream, IstreamHandler {
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
        const struct memcached_client_handler *handler;
        void *handler_ctx;

        IstreamPointer istream;

        Request(Istream &_istream,
                IstreamHandler &i_handler,
                const struct memcached_client_handler &_handler,
                void *_handler_ctx)
            :handler(&_handler), handler_ctx(_handler_ctx),
             istream(_istream, i_handler) {}

    } request;

    struct async_operation request_async;

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

    MemcachedClient(struct pool &_pool,
                    int fd, FdType fd_type,
                    Lease &lease,
                    Istream &_request,
                    const struct memcached_client_handler &_handler,
                    void *_handler_ctx,
                    struct async_operation_ref &async_ref);

    using Istream::GetPool;

    bool IsValid() const {
        return socket.IsValid();
    }

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
    void ReleaseSocket(bool reuse) {
        socket.Abandon();
        p_lease_release(lease_ref, reuse, GetPool());
    }

    void DestroySocket(bool reuse) {
        if (socket.IsConnected())
            ReleaseSocket(reuse);
        socket.Destroy();
    }

    /**
     * Release resources held by this object: the event object, the
     * socket lease, and the pool reference.
     */
    void Release(bool reuse) {
        if (socket.IsValid())
            DestroySocket(reuse);

        Destroy();
    }

    void AbortResponseHeaders(GError *error);
    void AbortResponseValue(GError *error);
    void AbortResponse(GError *error);

    BufferedResult SubmitResponse();
    BufferedResult BeginKey();
    BufferedResult FeedHeader(const void *data, size_t length);
    BufferedResult FeedExtras(const void *data, size_t length);
    BufferedResult FeedKey(const void *data, size_t length);
    BufferedResult FeedValue(const void *data, size_t length);
    BufferedResult Feed(const void *data, size_t length);

    DirectResult TryReadDirect(int fd, FdType type);

    void Abort();

    /* virtual methods from class Istream */

    off_t _GetAvailable(bool partial) override;
    void _Read() override;
    void _Close() override;

    /* virtual methods from class IstreamHandler */
    size_t OnData(const void *data, size_t length) override;
    void OnEof() override;
    void OnError(GError *error) override;
};

static const struct timeval memcached_client_timeout = {
    .tv_sec = 5,
    .tv_usec = 0,
};

void
MemcachedClient::AbortResponseHeaders(GError *error)
{
    assert(response.read_state == ReadState::HEADER ||
           response.read_state == ReadState::EXTRAS ||
           response.read_state == ReadState::KEY);

    request_async.Finished();

    if (socket.IsValid())
        DestroySocket(false);

    request.handler->error(error, request.handler_ctx);
    response.read_state = ReadState::END;

    if (request.istream.IsDefined())
        request.istream.ClearAndClose();

    Destroy();
}

void
MemcachedClient::AbortResponseValue(GError *error)
{
    assert(response.read_state == ReadState::VALUE);
    assert(!request.istream.IsDefined());

    if (socket.IsValid())
        DestroySocket(false);

    response.read_state = ReadState::END;
    DestroyError(error);
}

void
MemcachedClient::AbortResponse(GError *error)
{
    assert(response.read_state != ReadState::END);

    switch (response.read_state) {
    case ReadState::HEADER:
    case ReadState::EXTRAS:
    case ReadState::KEY:
        AbortResponseHeaders(error);
        return;

    case ReadState::VALUE:
        AbortResponseValue(error);
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
MemcachedClient::_GetAvailable(gcc_unused bool partial)
{
    assert(response.read_state == ReadState::VALUE);
    assert(!request.istream.IsDefined());

    return response.remaining;
}

void
MemcachedClient::_Read()
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
MemcachedClient::_Close()
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

    request_async.Finished();

    if (request.istream.IsDefined()) {
        /* at this point, the request must have been sent */
        GError *error =
            g_error_new_literal(memcached_client_quark(), 0,
                                "memcached server sends response too early");
        AbortResponseHeaders(error);
        return BufferedResult::CLOSED;
    }

    if (response.remaining > 0) {
        /* there's a value: pass it to the callback, continue
           reading */
        bool valid;

        response.read_state = ReadState::VALUE;

        const ScopePoolRef ref(GetPool() TRACE_ARGS);

        response.in_handler = true;
        request.handler->response((memcached_response_status)FromBE16(response.header.status),
                                  response.extras,
                                  response.header.extras_length,
                                  response.key.buffer,
                                  FromBE16(response.header.key_length),
                                  this, request.handler_ctx);
        response.in_handler = false;

        /* check if the callback has closed the value istream */
        valid = IsValid();

        if (valid && socket.IsConnected())
            socket.SetDirect(CheckDirect());

        return valid
            ? BufferedResult::AGAIN_EXPECT
            : BufferedResult::CLOSED;
    } else {
        /* no value: invoke the callback, quit */

        DestroySocket(socket.IsEmpty());

        response.read_state = ReadState::END;

        request.handler->response((memcached_response_status)FromBE16(response.header.status),
                                  response.extras,
                                  response.header.extras_length,
                                  response.key.buffer,
                                  FromBE16(response.header.key_length),
                                  nullptr, request.handler_ctx);

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
    socket.Consumed(sizeof(response.header));

    response.read_state = ReadState::EXTRAS;

    response.remaining = FromBE32(response.header.body_length);
    if (response.header.magic != MEMCACHED_MAGIC_RESPONSE ||
        FromBE16(response.header.key_length) +
        response.header.extras_length > response.remaining) {
        /* protocol error: abort the connection */
        GError *error =
            g_error_new_literal(memcached_client_quark(), 0,
                                "memcached protocol error");
        AbortResponseHeaders(error);
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

    socket.Consumed(response.header.extras_length);
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

    socket.Consumed(length);

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

    size_t nbytes = InvokeData(data, length);
    if (nbytes == 0)
        return IsValid()
            ? BufferedResult::BLOCKING
            : BufferedResult::CLOSED;

    socket.Consumed(nbytes);

    response.remaining -= nbytes;
    if (response.remaining > 0)
        return nbytes < length
            ? BufferedResult::PARTIAL
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
MemcachedClient::TryReadDirect(int fd, FdType type)
{
    assert(response.read_state == ReadState::VALUE);
    assert(response.remaining > 0);

    ssize_t nbytes = InvokeDirect(type, fd, response.remaining);
    if (likely(nbytes > 0)) {
        response.remaining -= nbytes;

        if (response.remaining == 0) {
            DestroySocket(true);
            DestroyEof();
            return DirectResult::CLOSED;
        } else
            return DirectResult::OK;
    } else if (unlikely(nbytes == ISTREAM_RESULT_EOF)) {
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

/**
 * The libevent callback for sending the request to the socket.
 */
static bool
memcached_client_socket_write(void *ctx)
{
    auto *client = (MemcachedClient *)ctx;
    assert(client->response.read_state != MemcachedClient::ReadState::END);

    const ScopePoolRef ref(client->GetPool() TRACE_ARGS);

    client->request.istream.Read();

    return client->socket.IsValid() && client->socket.IsConnected();
}

static BufferedResult
memcached_client_socket_data(const void *buffer, size_t size, void *ctx)
{
    auto *client = (MemcachedClient *)ctx;
    assert(client->response.read_state != MemcachedClient::ReadState::END);

    const ScopePoolRef ref(client->GetPool() TRACE_ARGS);
    return client->Feed(buffer, size);
}

static DirectResult
memcached_client_socket_direct(int fd, FdType type, void *ctx)
{
    auto *client = (MemcachedClient *)ctx;
    assert(client->response.read_state == MemcachedClient::ReadState::VALUE);
    assert(client->response.remaining > 0);
    assert(client->CheckDirect());

    return client->TryReadDirect(fd, type);
}

static bool
memcached_client_socket_closed(void *ctx)
{
    auto *client = (MemcachedClient *)ctx;

    /* the rest of the response may already be in the input buffer */
    client->ReleaseSocket(false);
    return true;
}

static bool
memcached_client_socket_remaining(gcc_unused size_t remaining, void *ctx)
{
    gcc_unused
    auto *client = (MemcachedClient *)ctx;

    /* only READ_VALUE could have blocked */
    assert(client->response.read_state == MemcachedClient::ReadState::VALUE);

    /* the rest of the response may already be in the input buffer */
    return true;
}

static void
memcached_client_socket_error(GError *error, void *ctx)
{
    auto *client = (MemcachedClient *)ctx;

    g_prefix_error(&error, "memcached connection failed: ");
    client->AbortResponse(error);
}

static constexpr BufferedSocketHandler memcached_client_socket_handler = {
    .data = memcached_client_socket_data,
    .direct = memcached_client_socket_direct,
    .closed = memcached_client_socket_closed,
    .remaining = memcached_client_socket_remaining,
    .end = nullptr,
    .write = memcached_client_socket_write,
    .drained = nullptr,
    .timeout = nullptr,
    .broken = nullptr,
    .error = memcached_client_socket_error,
};

/*
 * istream handler for the request
 *
 */

inline size_t
MemcachedClient::OnData(const void *data, size_t length)
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

        GError *error =
            g_error_new(memcached_client_quark(), 0,
                        "write error on memcached connection: %s",
                        strerror(errno));
        AbortResponse(error);
        return 0;
    }

    ScheduleWrite();
    return (size_t)nbytes;
}

inline void
MemcachedClient::OnEof()
{
    assert(request.istream.IsDefined());
    assert(response.read_state == ReadState::HEADER ||
           response.read_state == ReadState::EXTRAS ||
           response.read_state == ReadState::KEY);

    request.istream.Clear();

    socket.UnscheduleWrite();
    socket.Read(true);
}

inline void
MemcachedClient::OnError(GError *error)
{
    assert(request.istream.IsDefined());
    assert(response.read_state == ReadState::HEADER ||
           response.read_state == ReadState::EXTRAS ||
           response.read_state == ReadState::KEY);

    request.istream.Clear();
    AbortResponse(error);
}

/*
 * async operation
 *
 */

inline void
MemcachedClient::Abort()
{
    IstreamPointer request_istream = std::move(request.istream);

    /* async_operation_ref::Abort() can only be used before the
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
MemcachedClient::MemcachedClient(struct pool &_pool,
                                 int fd, FdType fd_type,
                                 Lease &lease,
                                 Istream &_request,
                                 const struct memcached_client_handler &_handler,
                                 void *_handler_ctx,
                                 struct async_operation_ref &async_ref)
    :Istream(_pool),
     request(_request, *this, _handler, _handler_ctx)
{
    socket.Init(GetPool(), fd, fd_type,
                nullptr, &memcached_client_timeout,
                memcached_client_socket_handler, this);

    p_lease_ref_set(lease_ref, lease, GetPool(), "memcached_client_lease");

    request_async.Init2<MemcachedClient,
                        &MemcachedClient::request_async>();
    async_ref.Set(request_async);

    response.read_state = MemcachedClient::ReadState::HEADER;

    request.istream.Read();
}

void
memcached_client_invoke(struct pool *pool,
                        int fd, FdType fd_type,
                        Lease &lease,
                        enum memcached_opcode opcode,
                        const void *extras, size_t extras_length,
                        const void *key, size_t key_length,
                        Istream *value,
                        const struct memcached_client_handler *handler,
                        void *handler_ctx,
                        struct async_operation_ref *async_ref)
{
    assert(extras_length <= MEMCACHED_EXTRAS_MAX);
    assert(key_length <= MEMCACHED_KEY_MAX);

    Istream *request = memcached_request_packet(*pool, opcode,
                                                extras, extras_length,
                                                key, key_length, value,
                                                0x1234 /* XXX? */);
    if (request == nullptr) {
        lease.ReleaseLease(true);

        GError *error =
            g_error_new_literal(memcached_client_quark(), 0,
                                "failed to generate memcached request packet");
        handler->error(error, handler_ctx);
        return;
    }

    NewFromPool<MemcachedClient>(*pool, *pool,
                                 fd, fd_type, lease,
                                 *request,
                                 *handler, handler_ctx, *async_ref);
}
