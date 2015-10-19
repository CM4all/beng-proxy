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
#include "pevent.hxx"
#include "istream/istream_internal.hxx"
#include "istream/istream_oo.hxx"
#include "pool.hxx"
#include "util/Cast.hxx"
#include "util/ByteOrder.hxx"

#include <daemon/log.h>

#include <glib.h>

#include <errno.h>
#include <sys/socket.h>
#include <string.h>

struct MemcachedClient {
    enum class ReadState {
        HEADER,
        EXTRAS,
        KEY,
        VALUE,
        END,
    };

    struct pool *pool, *caller_pool;

    /* I/O */
    BufferedSocket socket;
    struct lease_ref lease_ref;

    /* request */
    struct {
        const struct memcached_client_handler *handler;
        void *handler_ctx;

        struct istream *istream;
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

    struct istream response_value;

    bool IsValid() const {
        return socket.IsValid();
    }

    bool CheckDirect() const {
        assert(socket.IsConnected());
        assert(response.read_state == ReadState::VALUE);

        return istream_check_direct(&response_value, socket.GetType());
    }

    void ScheduleWrite() {
        socket.ScheduleWrite();
    }

    /**
     * Release the socket held by this object.
     */
    void ReleaseSocket(bool reuse) {
        socket.Abandon();
        p_lease_release(lease_ref, reuse, *pool);
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

        pool_unref(pool);
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

    /* istream handler */

    size_t OnData(const void *data, size_t length);

    ssize_t OnDirect(gcc_unused FdType type, gcc_unused int fd,
                     gcc_unused size_t max_length) {
        gcc_unreachable();
    }

    void OnEof();
    void OnError(GError *error);
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
    pool_unref(caller_pool);

    response.read_state = ReadState::END;

    if (request.istream != nullptr)
        istream_free_handler(&request.istream);

    pool_unref(pool);
}

void
MemcachedClient::AbortResponseValue(GError *error)
{
    assert(response.read_state == ReadState::VALUE);
    assert(request.istream == nullptr);

    if (socket.IsValid())
        DestroySocket(false);

    response.read_state = ReadState::END;
    istream_deinit_abort(&response_value, error);

    pool_unref(caller_pool);
    pool_unref(pool);
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

static inline MemcachedClient *
istream_to_memcached_client(struct istream *istream)
{
    return &ContainerCast2(*istream, &MemcachedClient::response_value);
}

static off_t
istream_memcached_available(struct istream *istream, gcc_unused bool partial)
{
    MemcachedClient *client = istream_to_memcached_client(istream);

    assert(client->response.read_state == MemcachedClient::ReadState::VALUE);
    assert(client->request.istream == nullptr);

    return client->response.remaining;
}

static void
istream_memcached_read(struct istream *istream)
{
    MemcachedClient *client = istream_to_memcached_client(istream);

    assert(client->response.read_state == MemcachedClient::ReadState::VALUE);
    assert(client->request.istream == nullptr);

    if (client->response.in_handler)
        /* avoid recursion; the memcached_client_handler caller will
           continue parsing the response if possible */
        return;

    if (client->socket.IsConnected())
        client->socket.SetDirect(client->CheckDirect());

    client->socket.Read(true);
}

static void
istream_memcached_close(struct istream *istream)
{
    MemcachedClient *client = istream_to_memcached_client(istream);
    struct pool *caller_pool = client->caller_pool;

    assert(client->response.read_state == MemcachedClient::ReadState::VALUE);
    assert(client->request.istream == nullptr);

    client->Release(false);

    istream_deinit(&client->response_value);
    pool_unref(caller_pool);
}

static const struct istream_class memcached_response_value = {
    .available = istream_memcached_available,
    .skip = nullptr,
    .read = istream_memcached_read,
    .as_fd = nullptr,
    .close = istream_memcached_close,
};

/*
 * response parser
 *
 */

BufferedResult
MemcachedClient::SubmitResponse()
{
    assert(response.read_state == ReadState::KEY);

    request_async.Finished();

    if (request.istream != nullptr) {
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
        struct istream *value;
        bool valid;

        response.read_state = ReadState::VALUE;

        istream_init(&response_value, &memcached_response_value, pool);
        value = &response_value;

        pool_ref(pool);

        /* we need this additional reference in case the handler
           closes the body */
        pool_ref(caller_pool);

        response.in_handler = true;
        request.handler->response((memcached_response_status)FromBE16(response.header.status),
                                  response.extras,
                                  response.header.extras_length,
                                  response.key.buffer,
                                  FromBE16(response.header.key_length),
                                  value, request.handler_ctx);
        response.in_handler = false;

        pool_unref(caller_pool);

        /* check if the callback has closed the value istream */
        valid = IsValid();

        if (valid && socket.IsConnected())
            socket.SetDirect(CheckDirect());

        pool_unref(pool);

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
        pool_unref(caller_pool);

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
        = (unsigned char *)p_malloc(pool,
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
        p_malloc(pool, response.header.extras_length);
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

    size_t nbytes = istream_invoke_data(&response_value, data, length);
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
    assert(request.istream == nullptr);

    response.read_state = ReadState::END;
    istream_deinit_eof(&response_value);
    pool_unref(caller_pool);

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

    ssize_t nbytes = istream_invoke_direct(&response_value, type, fd,
                                           response.remaining);
    if (likely(nbytes > 0)) {
        response.remaining -= nbytes;

        if (response.remaining == 0) {
            DestroySocket(true);
            istream_deinit_eof(&response_value);
            pool_unref(caller_pool);
            pool_unref(pool);
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

    const ScopePoolRef ref(*client->pool TRACE_ARGS);

    istream_read(client->request.istream);

    return client->socket.IsValid() && client->socket.IsConnected();
}

static BufferedResult
memcached_client_socket_data(const void *buffer, size_t size, void *ctx)
{
    auto *client = (MemcachedClient *)ctx;
    assert(client->response.read_state != MemcachedClient::ReadState::END);

    const ScopePoolRef ref(*client->pool TRACE_ARGS);
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
    assert(request.istream != nullptr);
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
    assert(request.istream != nullptr);
    assert(response.read_state == ReadState::HEADER ||
           response.read_state == ReadState::EXTRAS ||
           response.read_state == ReadState::KEY);

    request.istream = nullptr;

    socket.UnscheduleWrite();
    socket.Read(true);
}

inline void
MemcachedClient::OnError(GError *error)
{
    assert(request.istream != nullptr);
    assert(response.read_state == ReadState::HEADER ||
           response.read_state == ReadState::EXTRAS ||
           response.read_state == ReadState::KEY);

    request.istream = nullptr;
    AbortResponse(error);
}

/*
 * async operation
 *
 */

inline void
MemcachedClient::Abort()
{
    auto *caller_pool2 = caller_pool;
    struct istream *request_istream = request.istream;

    /* async_operation_ref::Abort() can only be used before the
       response was delivered to our callback */
    assert(response.read_state == ReadState::HEADER ||
           response.read_state == ReadState::EXTRAS ||
           response.read_state == ReadState::KEY);

    Release(false);
    pool_unref(caller_pool2);

    if (request_istream != nullptr)
        istream_close_handler(request_istream);
}

/*
 * constructor
 *
 */

void
memcached_client_invoke(struct pool *caller_pool,
                        int fd, FdType fd_type,
                        Lease &lease,
                        enum memcached_opcode opcode,
                        const void *extras, size_t extras_length,
                        const void *key, size_t key_length,
                        struct istream *value,
                        const struct memcached_client_handler *handler,
                        void *handler_ctx,
                        struct async_operation_ref *async_ref)
{
    struct istream *request;

    assert(extras_length <= MEMCACHED_EXTRAS_MAX);
    assert(key_length <= MEMCACHED_KEY_MAX);

    struct pool *pool = pool_new_linear(caller_pool, "memcached_client", 4096);

    request = memcached_request_packet(*pool, opcode, extras, extras_length,
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

    pool_ref(caller_pool);

    auto client = NewFromPool<MemcachedClient>(*pool);
    client->pool = pool;
    client->caller_pool = caller_pool;

    client->socket.Init(*pool, fd, fd_type,
                        nullptr, &memcached_client_timeout,
                        memcached_client_socket_handler, client);

    p_lease_ref_set(client->lease_ref, lease,
                    *pool, "memcached_client_lease");

    istream_assign_handler(&client->request.istream, request,
                           &MakeIstreamHandler<MemcachedClient>::handler, client,
                           0);

    client->request.handler = handler;
    client->request.handler_ctx = handler_ctx;

    client->request_async.Init2<MemcachedClient,
                                &MemcachedClient::request_async>();
    async_ref->Set(client->request_async);

    client->response.read_state = MemcachedClient::ReadState::HEADER;

    istream_read(client->request.istream);
}
