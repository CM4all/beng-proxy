/*
 * Listener on a TCP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_LISTENER_HXX
#define BENG_PROXY_LB_LISTENER_HXX

#include "net/ServerSocket.hxx"

class Error;
struct SslFactory;
struct LbListenerConfig;
struct LbInstance;

class lb_listener final : public ServerSocket {
public:
    LbInstance &instance;

    const LbListenerConfig &config;

    SslFactory *ssl_factory = nullptr;

    lb_listener(LbInstance &_instance,
                const LbListenerConfig &_config);
    ~lb_listener();

    bool Setup(Error &error);

    unsigned FlushSSLSessionCache(long tm);

protected:
    void OnAccept(SocketDescriptor &&fd, SocketAddress address) override;
    void OnAcceptError(std::exception_ptr ep) override;
};

#endif
