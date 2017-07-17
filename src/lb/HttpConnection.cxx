/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "HttpConnection.hxx"
#include "ClusterConfig.hxx"
#include "ListenerConfig.hxx"
#include "Goto.txx"
#include "ForwardHttpRequest.hxx"
#include "TranslationHandler.hxx"
#include "lb_instance.hxx"
#include "lb_cookie.hxx"
#include "http_server/http_server.hxx"
#include "http_server/Request.hxx"
#include "http_server/Handler.hxx"
#include "http_server/Error.hxx"
#include "SocketProtocolError.hxx"
#include "access_log.hxx"
#include "pool.hxx"
#include "address_string.hxx"
#include "thread_socket_filter.hxx"
#include "thread_pool.hxx"
#include "uri/uri_verify.hxx"
#include "ssl/ssl_filter.hxx"
#include "net/StaticSocketAddress.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "system/Error.hxx"
#include "util/Exception.hxx"

#include <assert.h>

LbHttpConnection::LbHttpConnection(struct pool &_pool, LbInstance &_instance,
                                   const LbListenerConfig &_listener,
                                   const LbGoto &_destination,
                                   SocketAddress _client_address)
    :pool(_pool), instance(_instance), listener(_listener),
     initial_destination(_destination),
     client_address(address_to_string(pool, _client_address)),
     logger(*this)
{
    if (client_address == nullptr)
        client_address = "unknown";
}

gcc_pure
static int
HttpServerLogLevel(std::exception_ptr e)
{
    try {
        FindRetrowNested<HttpServerSocketError>(e);
    } catch (const HttpServerSocketError &) {
        e = std::current_exception();

        /* some socket errors caused by our client are less
           important */

        try {
            FindRetrowNested<std::system_error>(e);
        } catch (const std::system_error &se) {
            if (se.code().category() == ErrnoCategory() &&
                se.code().value() == ECONNRESET)
                return 4;
            return 4;
        }

        try {
            FindRetrowNested<SocketProtocolError>(e);
        } catch (...) {
            return 4;
        }
    }

    return 2;
}

/*
 * public
 *
 */

LbHttpConnection *
NewLbHttpConnection(LbInstance &instance,
                    const LbListenerConfig &listener,
                    const LbGoto &destination,
                    SslFactory *ssl_factory,
                    UniqueSocketDescriptor &&fd, SocketAddress address)
{
    assert(listener.destination.GetProtocol() == LbProtocol::HTTP);

    /* determine the local socket address */
    StaticSocketAddress local_address = fd.GetLocalAddress();

    auto fd_type = FdType::FD_TCP;

    SslFilter *ssl_filter = nullptr;
    const SocketFilter *filter = nullptr;
    void *filter_ctx = nullptr;

    if (ssl_factory != nullptr) {
        ssl_filter = ssl_filter_new(*ssl_factory);
        filter = &thread_socket_filter;
        filter_ctx =
            new ThreadSocketFilter(instance.event_loop,
                                   thread_pool_get_queue(instance.event_loop),
                                   &ssl_filter_get_handler(*ssl_filter));
    }

    struct pool *pool = pool_new_linear(instance.root_pool,
                                        "http_connection",
                                        2048);
    pool_set_major(pool);

    auto *connection = NewFromPool<LbHttpConnection>(*pool, *pool, instance,
                                                     listener, destination,
                                                     address);
    connection->ssl_filter = ssl_filter;

    instance.http_connections.push_back(*connection);

    connection->http = http_server_connection_new(pool, instance.event_loop,
                                                  fd.Release(), fd_type,
                                                  filter, filter_ctx,
                                                  local_address.IsDefined()
                                                  ? (SocketAddress)local_address
                                                  : nullptr,
                                                  address,
                                                  false,
                                                  *connection);
    return connection;
}

void
LbHttpConnection::Destroy()
{
    assert(!instance.http_connections.empty());

    auto &connections = instance.http_connections;
    connections.erase(connections.iterator_to(*this));

    DeleteUnrefTrashPool(pool, this);
}

void
LbHttpConnection::CloseAndDestroy()
{
    assert(listener.destination.GetProtocol() == LbProtocol::HTTP);
    assert(http != nullptr);

    http_server_connection_close(http);

    Destroy();
}

void
LbHttpConnection::SendError(HttpServerRequest &request, std::exception_ptr ep)
{
    const char *msg = listener.verbose_response
        ? p_strdup(request.pool, GetFullMessage(ep).c_str())
        : "Bad gateway";

    http_server_send_message(&request, HTTP_STATUS_BAD_GATEWAY, msg);
}

void
LbHttpConnection::LogSendError(HttpServerRequest &request,
                               std::exception_ptr ep)
{
    logger(2, ep);
    SendError(request, ep);
}

static void
SendResponse(HttpServerRequest &request,
             const LbSimpleHttpResponse &response)
{
    assert(response.IsDefined());

    http_server_simple_response(request, response.status,
                                response.location.empty() ? nullptr : response.location.c_str(),
                                response.message.empty() ? nullptr : response.message.c_str());
}

/*
 * http connection handler
 *
 */

void
LbHttpConnection::HandleHttpRequest(HttpServerRequest &request,
                                    CancellablePointer &cancel_ptr)
{
    ++instance.http_request_counter;

    request_start_time = std::chrono::steady_clock::now();
    canonical_host = nullptr;

    if (!uri_path_verify_quick(request.uri)) {
        http_server_send_message(&request, HTTP_STATUS_BAD_REQUEST,
                                 "Malformed request URI");
        return;
    }

    HandleHttpRequest(initial_destination, request, cancel_ptr);
}

void
LbHttpConnection::HandleHttpRequest(const LbGoto &destination,
                                    HttpServerRequest &request,
                                    CancellablePointer &cancel_ptr)
{
    const auto &goto_ = destination.FindRequestLeaf(request);
    if (goto_.response != nullptr) {
        SendResponse(request, *goto_.response);
        return;
    }

    if (goto_.lua != nullptr) {
        InvokeLua(*goto_.lua, request, cancel_ptr);
        return;
    }

    if (goto_.translation != nullptr) {
        AskTranslationServer(*goto_.translation, request, cancel_ptr);
        return;
    }

    assert(goto_.cluster != nullptr);
    ForwardHttpRequest(*goto_.cluster, request, cancel_ptr);
}

void
LbHttpConnection::ForwardHttpRequest(LbCluster &cluster,
                                     HttpServerRequest &request,
                                     CancellablePointer &cancel_ptr)
{
    ::ForwardHttpRequest(*this, request, cluster, cancel_ptr);
}

void
LbHttpConnection::LogHttpRequest(HttpServerRequest &request,
                                 http_status_t status, int64_t length,
                                 uint64_t bytes_received, uint64_t bytes_sent)
{
    access_log(&request, nullptr,
               request.headers.Get("referer"),
               request.headers.Get("user-agent"),
               status, length,
               bytes_received, bytes_sent,
               std::chrono::steady_clock::now() - request_start_time);
}

void
LbHttpConnection::HttpConnectionError(std::exception_ptr e)
{
    logger(HttpServerLogLevel(e), e);

    assert(http != nullptr);
    http = nullptr;

    Destroy();
}

void
LbHttpConnection::HttpConnectionClosed()
{
    assert(http != nullptr);
    http = nullptr;

    Destroy();
}

std::string
LbHttpConnection::MakeLoggerDomain() const noexcept
{
    return "listener='" + listener.name
        + "' cluster='" + listener.destination.GetName()
        + "' client='" + client_address
        + "'";
}
