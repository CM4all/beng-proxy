/*
 * Call the translation server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "translate_client.hxx"
#include "translate_quark.hxx"
#include "translate_parser.hxx"
#include "translate_request.hxx"
#include "TranslateHandler.hxx"
#include "buffered_socket.hxx"
#include "please.hxx"
#include "growing_buffer.hxx"
#include "stopwatch.hxx"
#include "gerrno.h"
#include "pool.hxx"
#include "util/Cancellable.hxx"

#include <socket/address.h>

#include <glib.h>

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

    void Fail(GError *error);

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
TranslateClient::Fail(GError *error)
{
    stopwatch_event(stopwatch, "error");

    ReleaseSocket(false);

    handler.error(error, handler_ctx);
    pool_unref(&pool);
}


/*
 * request marshalling
 *
 */

static bool
write_packet_n(GrowingBuffer *gb, uint16_t command,
               const void *payload, size_t length, GError **error_r)
{
    static struct beng_translation_header header;

    if (length >= 0xffff) {
        g_set_error(error_r, translate_quark(), 0,
                    "payload for translate command %u too large",
                    command);
        return false;
    }

    header.length = (uint16_t)length;
    header.command = command;

    gb->Write(&header, sizeof(header));
    if (length > 0)
        gb->Write(payload, length);

    return true;
}

static bool
write_packet(GrowingBuffer *gb, uint16_t command,
             const char *payload, GError **error_r)
{
    return write_packet_n(gb, command, payload,
                          payload != nullptr ? strlen(payload) : 0,
                          error_r);
}

template<typename T>
static bool
write_buffer(GrowingBuffer *gb, uint16_t command,
             ConstBuffer<T> buffer, GError **error_r)
{
    auto b = buffer.ToVoid();
    return write_packet_n(gb, command, b.data, b.size, error_r);
}

/**
 * Forward the command to write_packet() only if #payload is not nullptr.
 */
static bool
write_optional_packet(GrowingBuffer *gb, uint16_t command,
                      const char *payload, GError **error_r)
{
    if (payload == nullptr)
        return true;

    return write_packet(gb, command, payload, error_r);
}

template<typename T>
static bool
write_optional_buffer(GrowingBuffer *gb, uint16_t command,
                      ConstBuffer<T> buffer, GError **error_r)
{
    return buffer.IsNull() || write_buffer(gb, command, buffer, error_r);
}

static bool
write_short(GrowingBuffer *gb,
            uint16_t command, uint16_t payload, GError **error_r)
{
    return write_packet_n(gb, command, &payload, sizeof(payload), error_r);
}

static bool
write_sockaddr(GrowingBuffer *gb,
               uint16_t command, uint16_t command_string,
               SocketAddress address,
               GError **error_r)
{
    assert(!address.IsNull());

    char address_string[1024];
    return write_packet_n(gb, command,
                          address.GetAddress(), address.GetSize(),
                          error_r) &&
        (!socket_address_to_string(address_string, sizeof(address_string),
                                   address.GetAddress(), address.GetSize()) ||
         write_packet(gb, command_string, address_string, error_r));
}

static bool
write_optional_sockaddr(GrowingBuffer *gb,
                        uint16_t command, uint16_t command_string,
                        SocketAddress address,
                        GError **error_r)
{
    return !address.IsNull()
        ? write_sockaddr(gb, command, command_string, address,
                         error_r)
        : true;
}

static GrowingBuffer
marshal_request(struct pool &pool, const TranslateRequest &request,
                GError **error_r)
{
    GrowingBuffer gb(pool, 512);

    bool success = write_packet_n(&gb, TRANSLATE_BEGIN,
                                  &PROTOCOL_VERSION, sizeof(PROTOCOL_VERSION),
                                  error_r) &&
        write_optional_buffer(&gb, TRANSLATE_ERROR_DOCUMENT,
                              request.error_document,
                              error_r) &&
        (request.error_document_status == 0 ||
         write_short(&gb, TRANSLATE_STATUS,
                     request.error_document_status, error_r)) &&
        write_optional_packet(&gb, TRANSLATE_LISTENER_TAG,
                              request.listener_tag, error_r) &&
        write_optional_sockaddr(&gb, TRANSLATE_LOCAL_ADDRESS,
                                TRANSLATE_LOCAL_ADDRESS_STRING,
                                request.local_address, error_r) &&
        write_optional_packet(&gb, TRANSLATE_REMOTE_HOST,
                              request.remote_host, error_r) &&
        write_optional_packet(&gb, TRANSLATE_HOST, request.host, error_r) &&
        write_optional_packet(&gb, TRANSLATE_USER_AGENT, request.user_agent,
                              error_r) &&
        write_optional_packet(&gb, TRANSLATE_UA_CLASS, request.ua_class,
                              error_r) &&
        write_optional_packet(&gb, TRANSLATE_LANGUAGE,
                              request.accept_language, error_r) &&
        write_optional_packet(&gb, TRANSLATE_AUTHORIZATION,
                              request.authorization, error_r) &&
        write_optional_packet(&gb, TRANSLATE_URI, request.uri, error_r) &&
        write_optional_packet(&gb, TRANSLATE_ARGS, request.args, error_r) &&
        write_optional_packet(&gb, TRANSLATE_QUERY_STRING,
                              request.query_string, error_r) &&
        write_optional_packet(&gb, TRANSLATE_WIDGET_TYPE,
                              request.widget_type, error_r) &&
        write_optional_buffer(&gb, TRANSLATE_SESSION, request.session,
                              error_r) &&
        write_optional_buffer(&gb, TRANSLATE_INTERNAL_REDIRECT,
                              request.internal_redirect, error_r) &&
        write_optional_buffer(&gb, TRANSLATE_CHECK, request.check,
                              error_r) &&
        write_optional_buffer(&gb, TRANSLATE_AUTH, request.auth, error_r) &&
        write_optional_buffer(&gb, TRANSLATE_WANT_FULL_URI,
                              request.want_full_uri, error_r) &&
        write_optional_buffer(&gb, TRANSLATE_WANT, request.want, error_r) &&
        write_optional_buffer(&gb, TRANSLATE_FILE_NOT_FOUND,
                              request.file_not_found, error_r) &&
        write_optional_buffer(&gb, TRANSLATE_CONTENT_TYPE_LOOKUP,
                              request.content_type_lookup, error_r) &&
        write_optional_packet(&gb, TRANSLATE_SUFFIX, request.suffix,
                              error_r) &&
        write_optional_buffer(&gb, TRANSLATE_ENOTDIR,
                              request.enotdir, error_r) &&
        write_optional_buffer(&gb, TRANSLATE_DIRECTORY_INDEX,
                              request.directory_index, error_r) &&
        write_optional_packet(&gb, TRANSLATE_PARAM, request.param,
                              error_r) &&
        write_optional_buffer(&gb, TRANSLATE_PROBE_PATH_SUFFIXES,
                              request.probe_path_suffixes,
                              error_r) &&
        write_optional_packet(&gb, TRANSLATE_PROBE_SUFFIX,
                              request.probe_suffix,
                              error_r) &&
        write_optional_buffer(&gb, TRANSLATE_READ_FILE,
                              request.read_file,
                              error_r) &&
        write_optional_packet(&gb, TRANSLATE_USER,
                              request.user, error_r) &&
        write_packet(&gb, TRANSLATE_END, nullptr, error_r);
    if (!success)
        gb.Clear();

    return gb;
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
    Fail(g_error_new_literal(translate_quark(), 0, e.what()));
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

        GError *error =
            new_error_errno_msg("write error to translation server");
        client->Fail(error);
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
translate_client_socket_error(GError *error, void *ctx)
{
    TranslateClient *client = (TranslateClient *)ctx;

    g_prefix_error(&error, "Translation server connection failed: ");
    client->Fail(error);
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
{
    assert(fd >= 0);
    assert(request.uri != nullptr || request.widget_type != nullptr ||
           (!request.content_type_lookup.IsNull() &&
            request.suffix != nullptr));
    assert(handler.response != nullptr);
    assert(handler.error != nullptr);

    GError *error = nullptr;
    GrowingBuffer gb = marshal_request(pool, request, &error);
    if (gb.IsEmpty()) {
        lease.ReleaseLease(true);

        handler.error(error, ctx);
        return;
    }

    auto *client = NewFromPool<TranslateClient>(pool, pool, event_loop,
                                                fd, lease,
                                                request, std::move(gb),
                                                handler, ctx, cancel_ptr);

    pool_ref(&client->pool);
    translate_try_write(client);
}
