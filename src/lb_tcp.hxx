/*
 * Handler for raw TCP connections.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_TCP_H
#define BENG_PROXY_LB_TCP_H

#include "filtered_socket.hxx"
#include "StickyHash.hxx"
#include "net/StaticSocketAddress.hxx"
#include "net/ConnectSocket.hxx"
#include "io/FdType.hxx"
#include "util/Cancellable.hxx"

#include <exception>

struct pool;
class EventLoop;
class Stock;
struct AddressList;
struct Balancer;
struct LbClusterConfig;
struct LbTcpConnection;
class UniqueSocketDescriptor;
class LbClusterMap;

class LbTcpConnectionHandler {
public:
    virtual void OnTcpEnd() = 0;
    virtual void OnTcpError(const char *prefix, const char *error) = 0;
    virtual void OnTcpErrno(const char *prefix, int error) = 0;
    virtual void OnTcpError(const char *prefix, std::exception_ptr ep) = 0;
};

struct LbTcpConnection final : ConnectSocketHandler {
    struct pool &pool;
    Stock *pipe_stock;

    LbTcpConnectionHandler &handler;

    FilteredSocket inbound;

    BufferedSocket outbound;

    StaticSocketAddress bind_address;
    const LbClusterConfig &cluster;
    LbClusterMap &clusters;
    Balancer &balancer;
    const sticky_hash_t session_sticky;

    CancellablePointer cancel_connect;

    bool got_inbound_data, got_outbound_data;

    LbTcpConnection(struct pool &_pool, EventLoop &event_loop,
                    Stock *_pipe_stock,
                    UniqueSocketDescriptor &&fd, FdType fd_type,
                    const SocketFilter *filter, void *filter_ctx,
                    SocketAddress remote_address,
                    const LbClusterConfig &cluster,
                    LbClusterMap &clusters,
                    Balancer &balancer,
                    LbTcpConnectionHandler &_handler);

    ~LbTcpConnection() {
        Destroy();
    }

    void ScheduleHandshakeCallback() {
        inbound.ScheduleReadNoTimeout(false);
        inbound.SetHandshakeCallback(BIND_THIS_METHOD(OnHandshake));
    }

    void ConnectOutbound();

    void DestroyInbound();
    void DestroyOutbound();
    void Destroy();

    void OnHandshake();

    /* virtual methods from class ConnectSocketHandler */
    void OnSocketConnectSuccess(UniqueSocketDescriptor &&fd) override;
    void OnSocketConnectTimeout() override;
    void OnSocketConnectError(std::exception_ptr ep) override;
};

#endif
