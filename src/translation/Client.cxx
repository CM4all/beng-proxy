/*
 * Call the translation server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Client.hxx"
#include "Marshal.hxx"
#include "Parser.hxx"
#include "Request.hxx"
#include "Handler.hxx"
#include "buffered_socket.hxx"
#include "please.hxx"
#include "stopwatch.hxx"
#include "GException.hxx"
#include "gerrno.h"
#include "pool.hxx"
#include "system/Error.hxx"
#include "util/Cancellable.hxx"
#include "util/Exception.hxx"

#include <stdexcept>

#include <assert.h>
#include <string.h>
#include <errno.h>

static const uint8_t PROTOCOL_VERSION = 3;

struct TranslateClient final : Cancellable {
    struct pool &pool;

    Stopwatch *const stopwatch;

    BufferedSocket socket;
    struct lease_ref lease_ref;


    /** the marshalled translate request */
    GrowingBufferReader request_buffer;
    GrowingBufferReader request;

    const TranslateHandler &handler;
    void *handler_ctx;

    TranslateParser parser;

    TranslateClient(struct pool &p, EventLoop &event_loop,
                    int fd, Lease &lease,
                    const TranslateRequest &request2,
                    GrowingBuffer &&_request,
                    const TranslateHandler &_handler, void *_ctx,
                    CancellablePointer &cancel_ptr);

    void ReleaseSocket(bool reuse);
    void Release(bool reuse);

    void Fail(std::exception_ptr ep);
    void Fail(const std::exception &e);

    BufferedResult Feed(const uint8_t *data, size_t length);

    /* virtual methods from class Cancellable */
    void Cancel() override {
        stopwatch_event(stopwatch, "cancel");
        Release(false);
    }
};

static constexpr struct timeval translate_read_timeout = {
    .tv_sec = 60,
    .tv_usec = 0,
};

static constexpr struct timeval translate_write_timeout = {
    .tv_sec = 10,
    .tv_usec = 0,
};

void
TranslateClient::ReleaseSocket(bool reuse)
{
    assert(socket.IsConnected());

    stopwatch_dump(stopwatch);

    socket.Abandon();
    socket.Destroy();

    p_lease_release(lease_ref, reuse, pool);
}

/**
 * Release resources held by this object: the event object, the socket
 * lease, and the pool reference.
 */
void
TranslateClient::Release(bool reuse)
{
    ReleaseSocket(reuse);
    pool_unref(&pool);
}

void
TranslateClient::Fail(std::exception_ptr ep)
{
    stopwatch_event(stopwatch, "error");

    ReleaseSocket(false);

    handler.error(ep, handler_ctx);
    pool_unref(&pool);
}

void
TranslateClient::Fail(const std::exception &e)
{
    Fail(std::make_exception_ptr(e));
}

/*
 * receive response
 *
 */

inline BufferedResult
TranslateClient::Feed(const uint8_t *data, size_t length)
try {
    size_t consumed = 0;
    while (consumed < length) {
        size_t nbytes = parser.Feed(data + consumed, length - consumed);
        if (nbytes == 0)
            /* need more data */
            break;

        consumed += nbytes;
        socket.Consumed(nbytes);

        auto result = parser.Process();
        switch (result) {
        case TranslateParser::Result::MORE:
            break;

        case TranslateParser::Result::DONE:
            ReleaseSocket(true);
            handler.response(parser.GetResponse(), handler_ctx);
            pool_unref(&pool);
            return BufferedResult::CLOSED;
        }
    }

    return BufferedResult::MORE;
} catch (const std::runtime_error &e) {
    Fail(e);
    return BufferedResult::CLOSED;
}

/*
 * send requests
 *
 */

static bool
translate_try_write(TranslateClient *client)
{
    auto src = client->request.Read();
    assert(!src.IsNull());

    ssize_t nbytes = client->socket.Write(src.data, src.size);
    if (gcc_unlikely(nbytes < 0)) {
        if (gcc_likely(nbytes == WRITE_BLOCKING))
            return true;

        client->Fail(MakeErrno("write error to translation server"));
        return false;
    }

    client->request.Consume(nbytes);
    if (client->request.IsEOF()) {
        /* the buffer is empty, i.e. the request has been sent */

        stopwatch_event(client->stopwatch, "request");

        client->socket.UnscheduleWrite();
        return client->socket.Read(true);
    }

    client->socket.ScheduleWrite();
    return true;
}


/*
 * buffered_socket handler
 *
 */

static BufferedResult
translate_client_socket_data(const void *buffer, size_t size, void *ctx)
{
    TranslateClient *client = (TranslateClient *)ctx;

    return client->Feed((const uint8_t *)buffer, size);
}

static bool
translate_client_socket_closed(void *ctx)
{
    TranslateClient *client = (TranslateClient *)ctx;

    client->ReleaseSocket(false);
    return true;
}

static bool
translate_client_socket_write(void *ctx)
{
    TranslateClient *client = (TranslateClient *)ctx;

    return translate_try_write(client);
}

static void
translate_client_socket_error(std::exception_ptr ep, void *ctx)
{
    TranslateClient *client = (TranslateClient *)ctx;

    client->Fail(NestException(ep,
                               std::runtime_error("Translation server connection failed")));
}

static constexpr BufferedSocketHandler translate_client_socket_handler = {
    .data = translate_client_socket_data,
    .direct = nullptr,
    .closed = translate_client_socket_closed,
    .remaining = nullptr,
    .end = nullptr,
    .write = translate_client_socket_write,
    .drained = nullptr,
    .timeout = nullptr,
    .broken = nullptr,
    .error = translate_client_socket_error,
};

/*
 * constructor
 *
 */

inline
TranslateClient::TranslateClient(struct pool &p, EventLoop &event_loop,
                                 int fd, Lease &lease,
                                 const TranslateRequest &request2,
                                 GrowingBuffer &&_request,
                                 const TranslateHandler &_handler, void *_ctx,
                                 CancellablePointer &cancel_ptr)
    :pool(p),
     stopwatch(stopwatch_fd_new(&p, fd, request2.GetDiagnosticName())),
     socket(event_loop),
     request_buffer(std::move(_request)),
     request(request_buffer),
     handler(_handler), handler_ctx(_ctx),
     parser(p, request2)
{
    socket.Init(fd, FdType::FD_SOCKET,
                &translate_read_timeout,
                &translate_write_timeout,
                translate_client_socket_handler, this);
    p_lease_ref_set(lease_ref, lease, p, "translate_lease");

    cancel_ptr = *this;
}

void
translate(struct pool &pool, EventLoop &event_loop,
          int fd, Lease &lease,
          const TranslateRequest &request,
          const TranslateHandler &handler, void *ctx,
          CancellablePointer &cancel_ptr)
try {
    assert(fd >= 0);
    assert(request.uri != nullptr || request.widget_type != nullptr ||
           (!request.content_type_lookup.IsNull() &&
            request.suffix != nullptr));
    assert(handler.response != nullptr);
    assert(handler.error != nullptr);

    GrowingBuffer gb = MarshalTranslateRequest(pool, PROTOCOL_VERSION,
                                               request);

    auto *client = NewFromPool<TranslateClient>(pool, pool, event_loop,
                                                fd, lease,
                                                request, std::move(gb),
                                                handler, ctx, cancel_ptr);

    pool_ref(&client->pool);
    translate_try_write(client);
} catch (...) {
    lease.ReleaseLease(true);

    handler.error(std::current_exception(), ctx);
}
