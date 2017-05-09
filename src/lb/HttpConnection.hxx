/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_HTTP_CONNECTION_HXX
#define BENG_PROXY_LB_HTTP_CONNECTION_HXX

#include "http_server/Handler.hxx"
#include "Logger.hxx"

#include <boost/intrusive/list.hpp>

#include <chrono>

#include <stdint.h>

struct pool;
struct SslFactory;
struct SslFilter;
struct ThreadSocketFilter;
class UniqueSocketDescriptor;
class SocketAddress;
struct HttpServerConnection;
struct LbListenerConfig;
struct LbClusterConfig;
struct LbGoto;
struct LbInstance;

struct LbHttpConnection final
    : HttpServerConnectionHandler, Logger,
      boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

    struct pool &pool;

    LbInstance &instance;

    const LbListenerConfig &listener;

    /**
     * The client's address formatted as a string (for logging).  This
     * is guaranteed to be non-nullptr.
     */
    const char *client_address;

    SslFilter *ssl_filter = nullptr;

    HttpServerConnection *http;

    /**
     * The time stamp at the start of the request.  Used to calculate
     * the request duration.
     */
    std::chrono::steady_clock::time_point request_start_time;

    LbHttpConnection(struct pool &_pool, LbInstance &_instance,
                     const LbListenerConfig &_listener,
                     SocketAddress _client_address);

    void Destroy();
    void CloseAndDestroy();

    /* virtual methods from class HttpServerConnectionHandler */
    void HandleHttpRequest(HttpServerRequest &request,
                           CancellablePointer &cancel_ptr) override;

    void LogHttpRequest(HttpServerRequest &request,
                        http_status_t status, off_t length,
                        uint64_t bytes_received, uint64_t bytes_sent) override;

    void HttpConnectionError(GError *error) override;
    void HttpConnectionClosed() override;

private:
    void HandleHttpRequest(const LbGoto &destination,
                           HttpServerRequest &request,
                           CancellablePointer &cancel_ptr);
    void ForwardHttpRequest(const LbClusterConfig &cluster_config,
                            HttpServerRequest &request,
                            CancellablePointer &cancel_ptr);

protected:
    /* virtual methods from class Logger */
    std::string MakeLogName() const noexcept override;
};

LbHttpConnection *
NewLbHttpConnection(LbInstance &instance,
                    const LbListenerConfig &listener,
                    SslFactory *ssl_factory,
                    UniqueSocketDescriptor &&fd, SocketAddress address);

#endif
