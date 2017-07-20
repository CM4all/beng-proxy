/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_HTTP_CONNECTION_HXX
#define BENG_PROXY_LB_HTTP_CONNECTION_HXX

#include "http_server/Handler.hxx"
#include "io/Logger.hxx"

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
class LbCluster;
class LbLuaHandler;
class LbTranslationHandler;
struct LbGoto;
class LbTranslationHandler;
struct LbInstance;

struct LbHttpConnection final
    : HttpServerConnectionHandler, LoggerDomainFactory,
      boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

    struct pool &pool;

    LbInstance &instance;

    const LbListenerConfig &listener;

    const LbGoto &initial_destination;

    /**
     * The client's address formatted as a string (for logging).  This
     * is guaranteed to be non-nullptr.
     */
    const char *client_address;

    const LazyDomainLogger logger;

    SslFilter *ssl_filter = nullptr;

    HttpServerConnection *http;

    /**
     * The time stamp at the start of the request.  Used to calculate
     * the request duration.
     */
    std::chrono::steady_clock::time_point request_start_time;

    /**
     * The current request's canonical host name (from
     * #TRANSLATE_CANONICAL_HOST).  If set, then the string is
     * allocated from the request pool, and is only valid for that one
     * request.  It must be cleared each time a new request starts.
     */
    const char *canonical_host;

    LbHttpConnection(struct pool &_pool, LbInstance &_instance,
                     const LbListenerConfig &_listener,
                     const LbGoto &_destination,
                     SocketAddress _client_address);

    void Destroy();
    void CloseAndDestroy();

    bool IsEncrypted() const {
        return ssl_filter != nullptr;
    }

    void SendError(HttpServerRequest &request, std::exception_ptr ep);
    void LogSendError(HttpServerRequest &request, std::exception_ptr ep);

    /* virtual methods from class HttpServerConnectionHandler */
    void HandleHttpRequest(HttpServerRequest &request,
                           CancellablePointer &cancel_ptr) override;

    void LogHttpRequest(HttpServerRequest &request,
                        http_status_t status, off_t length,
                        uint64_t bytes_received, uint64_t bytes_sent) override;

    void HttpConnectionError(std::exception_ptr e) override;
    void HttpConnectionClosed() override;

public:
    void HandleHttpRequest(const LbGoto &destination,
                           HttpServerRequest &request,
                           CancellablePointer &cancel_ptr);

private:
    void ForwardHttpRequest(LbCluster &cluster,
                            HttpServerRequest &request,
                            CancellablePointer &cancel_ptr);

    void InvokeLua(LbLuaHandler &handler,
                   HttpServerRequest &request,
                   CancellablePointer &cancel_ptr);

    void AskTranslationServer(LbTranslationHandler &handler,
                              HttpServerRequest &request,
                              CancellablePointer &cancel_ptr);

protected:
    /* virtual methods from class LoggerDomainFactory */
    std::string MakeLoggerDomain() const noexcept override;
};

LbHttpConnection *
NewLbHttpConnection(LbInstance &instance,
                    const LbListenerConfig &listener,
                    const LbGoto &destination,
                    SslFactory *ssl_factory,
                    UniqueSocketDescriptor &&fd, SocketAddress address);

#endif
