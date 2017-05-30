/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_CONNECTION_H
#define BENG_PROXY_LB_CONNECTION_H

#include "filtered_socket.hxx"
#include "StickyHash.hxx"
#include "Logger.hxx"
#include "net/StaticSocketAddress.hxx"
#include "net/ConnectSocket.hxx"
#include "io/FdType.hxx"
#include "util/Cancellable.hxx"

#include <boost/intrusive/list.hpp>

#include <exception>

#include <stdint.h>

struct pool;
struct SslFactory;
struct SslFilter;
struct ThreadSocketFilter;
class UniqueSocketDescriptor;
class SocketAddress;
struct LbListenerConfig;
struct LbClusterConfig;
struct LbGoto;
struct LbInstance;

class LbTcpConnection final
    : public Logger, ConnectSocketHandler,
      public boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

    struct pool &pool;

    LbInstance &instance;

    const LbListenerConfig &listener;
    const LbClusterConfig &cluster;

    /**
     * The client's address formatted as a string (for logging).  This
     * is guaranteed to be non-nullptr.
     */
    const char *client_address;

    const sticky_hash_t session_sticky;

public:
    FilteredSocket inbound;
    BufferedSocket outbound;

    StaticSocketAddress bind_address;
    CancellablePointer cancel_connect;

    bool got_inbound_data, got_outbound_data;

    LbTcpConnection(struct pool &_pool, LbInstance &_instance,
                    const LbListenerConfig &_listener,
                    UniqueSocketDescriptor &&fd, FdType fd_type,
                    const SocketFilter *filter, void *filter_ctx,
                    SocketAddress _client_address);

    ~LbTcpConnection();

    static LbTcpConnection *New(LbInstance &instance,
                                const LbListenerConfig &listener,
                                SslFactory *ssl_factory,
                                UniqueSocketDescriptor &&fd,
                                SocketAddress address);

    void Destroy();

protected:
    /* virtual methods from class Logger */
    std::string MakeLogName() const noexcept override;

private:
    void ScheduleHandshakeCallback() {
        inbound.ScheduleReadNoTimeout(false);
        inbound.SetHandshakeCallback(BIND_THIS_METHOD(OnHandshake));
    }

    void ConnectOutbound();

public:
    void DestroyInbound();
    void DestroyOutbound();
    void DestroyBoth();

    void OnHandshake();

    void OnTcpEnd();
    void OnTcpError(const char *prefix, const char *error);
    void OnTcpErrno(const char *prefix, int error);
    void OnTcpError(const char *prefix, std::exception_ptr ep);

private:
    /* virtual methods from class ConnectSocketHandler */
    void OnSocketConnectSuccess(UniqueSocketDescriptor &&fd) override;
    void OnSocketConnectTimeout() override;
    void OnSocketConnectError(std::exception_ptr ep) override;
};

#endif
