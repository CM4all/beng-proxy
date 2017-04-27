/*
 * Manage connections to HTTP clients.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_CONNECTION_H
#define BENG_PROXY_LB_CONNECTION_H

#include "http_server/Handler.hxx"

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
struct LbTcpConnection;
struct LbInstance;

struct LbConnection final
    : HttpServerConnectionHandler,
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
    ThreadSocketFilter *thread_socket_filter = nullptr;

    HttpServerConnection *http;

    /**
     * The time stamp at the start of the request.  Used to calculate
     * the request duration.
     */
    std::chrono::steady_clock::time_point request_start_time;

    LbTcpConnection *tcp;

    LbConnection(struct pool &_pool, LbInstance &_instance,
                 const LbListenerConfig &_listener,
                 SocketAddress _client_address);

    /* virtual methods from class HttpServerConnectionHandler */
    void HandleHttpRequest(HttpServerRequest &request,
                           CancellablePointer &cancel_ptr) override;

    void LogHttpRequest(HttpServerRequest &request,
                        http_status_t status, off_t length,
                        uint64_t bytes_received, uint64_t bytes_sent) override;

    void HttpConnectionError(GError *error) override;
    void HttpConnectionClosed() override;

private:
    void ForwardHttpRequest(const LbClusterConfig &cluster_config,
                            HttpServerRequest &request,
                            CancellablePointer &cancel_ptr);
};

LbConnection *
lb_connection_new(LbInstance &instance,
                  const LbListenerConfig &listener,
                  SslFactory *ssl_factory,
                  UniqueSocketDescriptor &&fd, SocketAddress address);

void
lb_connection_remove(LbConnection *connection);

void
lb_connection_close(LbConnection *connection);

#endif
