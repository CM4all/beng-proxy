/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_CONNECTION_H
#define BENG_PROXY_LB_CONNECTION_H

#include "lb_tcp.hxx"
#include "Logger.hxx"

#include <boost/intrusive/list.hpp>

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
struct LbTcpConnection;
struct LbInstance;

struct LbConnection final
    : Logger, LbTcpConnectionHandler,
      boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

    struct pool &pool;

    LbInstance &instance;

    const LbListenerConfig &listener;

    /**
     * The client's address formatted as a string (for logging).  This
     * is guaranteed to be non-nullptr.
     */
    const char *client_address;

    LbTcpConnection *tcp;

    LbConnection(struct pool &_pool, LbInstance &_instance,
                 const LbListenerConfig &_listener,
                 UniqueSocketDescriptor &&fd, FdType fd_type,
                 const SocketFilter *filter, void *filter_ctx,
                 SocketAddress _client_address);

protected:
    /* virtual methods from class Logger */
    std::string MakeLogName() const noexcept override;

private:
    /* virtual methods from class LbTcpConnectionHandler */
    void OnTcpEnd() override;
    void OnTcpError(const char *prefix, const char *error) override;
    void OnTcpErrno(const char *prefix, int error) override;
    void OnTcpError(const char *prefix, std::exception_ptr ep) override;
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
