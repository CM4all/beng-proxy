/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "HttpConnection.hxx"
#include "lb/ForwardHttpRequest.hxx"
#include "lb_instance.hxx"
#include "lb_config.hxx"
#include "lb_cookie.hxx"
#include "lb_log.hxx"
#include "http_server/http_server.hxx"
#include "http_server/Request.hxx"
#include "http_server/Handler.hxx"
#include "http_response.hxx"
#include "http_headers.hxx"
#include "access_log.hxx"
#include "pool.hxx"
#include "gerrno.h"
#include "GException.hxx"
#include "address_string.hxx"
#include "thread_socket_filter.hxx"
#include "thread_pool.hxx"
#include "ssl/ssl_filter.hxx"
#include "net/StaticSocketAddress.hxx"
#include "net/UniqueSocketDescriptor.hxx"

#include <daemon/log.h>

#include <assert.h>

LbHttpConnection::LbHttpConnection(struct pool &_pool, LbInstance &_instance,
                                   const LbListenerConfig &_listener,
                                   SocketAddress _client_address)
    :pool(_pool), instance(_instance), listener(_listener),
     client_address(address_to_string(pool, _client_address))
{
    if (client_address == nullptr)
        client_address = "unknown";
}

/*
 * public
 *
 */

LbHttpConnection *
NewLbHttpConnection(LbInstance &instance,
                    const LbListenerConfig &listener,
                    SslFactory *ssl_factory,
                    UniqueSocketDescriptor &&fd, SocketAddress address)
{
    assert(listener.destination.GetProtocol() == LbProtocol::HTTP);

    /* determine the local socket address */
    StaticSocketAddress local_address = fd.GetLocalAddress();

    struct pool *pool = pool_new_linear(instance.root_pool,
                                        "http_connection",
                                        2048);
    pool_set_major(pool);

    auto *connection = NewFromPool<LbHttpConnection>(*pool, *pool, instance,
                                                     listener, address);

    auto fd_type = FdType::FD_TCP;

    const SocketFilter *filter = nullptr;
    void *filter_ctx = nullptr;

    if (ssl_factory != nullptr) {
        try {
            connection->ssl_filter = ssl_filter_new(*ssl_factory);
        } catch (const std::runtime_error &e) {
            lb_connection_log_error(1, connection, "SSL", e);
            pool_unref(pool);
            return nullptr;
        }

        filter = &thread_socket_filter;
        filter_ctx = connection->thread_socket_filter =
            new ThreadSocketFilter(instance.event_loop,
                                   thread_pool_get_queue(instance.event_loop),
                                   &ssl_filter_get_handler(*connection->ssl_filter));
    }

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

    struct pool &p = pool;
    pool_trash(&p);
    pool_unref(&p);
}

void
LbHttpConnection::CloseAndDestroy()
{
    assert(listener.destination.GetProtocol() == LbProtocol::HTTP);
    assert(http != nullptr);

    http_server_connection_close(http);

    Destroy();
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

class LbLuaResponseHandler final : public HttpResponseHandler {
    LbHttpConnection &connection;

    HttpServerRequest &request;

    bool finished = false;

public:
    LbLuaResponseHandler(LbHttpConnection &_connection,
                         HttpServerRequest &_request)
        :connection(_connection), request(_request) {}

    bool IsFinished() const {
        return finished;
    }

    /* virtual methods from class HttpResponseHandler */
    void OnHttpResponse(http_status_t status, StringMap &&headers,
                        Istream *body) override;
    void OnHttpError(GError *error) override;
};

void
LbLuaResponseHandler::OnHttpResponse(http_status_t status,
                                     StringMap &&_headers,
                                     Istream *response_body)
{
    finished = true;

    HttpHeaders headers(std::move(_headers));

    if (request.method == HTTP_METHOD_HEAD)
        /* pass Content-Length, even though there is no response body
           (RFC 2616 14.13) */
        headers.MoveToBuffer("content-length");

    http_server_response(&request, status, std::move(headers), response_body);
}

void
LbLuaResponseHandler::OnHttpError(GError *error)
{
    finished = true;

    lb_connection_log_gerror(2, &connection, "Error", error);

    const char *msg = connection.listener.verbose_response
        ? error->message
        : "Server failure";

    http_server_send_message(&request, HTTP_STATUS_BAD_GATEWAY, msg);

    g_error_free(error);
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

    HandleHttpRequest(listener.destination, request, cancel_ptr);
}

void
LbHttpConnection::HandleHttpRequest(const LbGoto &destination,
                                    HttpServerRequest &request,
                                    CancellablePointer &cancel_ptr)
{
    const auto &goto_ = destination.FindRequestLeaf(request);
    if (goto_.response.IsDefined()) {
        SendResponse(request, goto_.response);
        return;
    }

    if (goto_.lua != nullptr) {
        auto *handler = instance.lua_handlers.Find(goto_.lua->name);
        assert(handler != nullptr);

        LbLuaResponseHandler response_handler(*this, request);
        const LbGoto *g;

        try {
            g = handler->HandleRequest(request, response_handler);
        } catch (const std::runtime_error &e) {
            if (response_handler.IsFinished())
                daemon_log(1, "Lua error: %s\n", e.what());
            else
                response_handler.InvokeError(ToGError(e));
            return;
        }

        if (response_handler.IsFinished())
            return;

        if (g == nullptr) {
            http_server_send_message(&request, HTTP_STATUS_BAD_GATEWAY,
                                     "No response from Lua handler");
            return;
        }

        HandleHttpRequest(*g, request, cancel_ptr);
        return;
    }

    assert(goto_.cluster != nullptr);
    ForwardHttpRequest(*goto_.cluster, request, cancel_ptr);
}

inline void
LbHttpConnection::ForwardHttpRequest(const LbClusterConfig &cluster_config,
                                     HttpServerRequest &request,
                                     CancellablePointer &cancel_ptr)
{
    ::ForwardHttpRequest(*this, request, cluster_config, cancel_ptr);
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
LbHttpConnection::HttpConnectionError(GError *error)
{
    int level = 2;

    if (error->domain == errno_quark() && error->code == ECONNRESET)
        level = 4;

    lb_connection_log_gerror(level, this, "Error", error);
    g_error_free(error);

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
