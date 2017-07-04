/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_LB_LISTENER_HXX
#define BENG_LB_LISTENER_HXX

#include "Goto.hxx"
#include "io/Logger.hxx"
#include "net/ServerSocket.hxx"

struct SslFactory;
struct LbListenerConfig;
struct LbInstance;
class LbGotoMap;

/**
 * Listener on a TCP port.
 */
class LbListener final : public ServerSocket {
    LbInstance &instance;

    const LbListenerConfig &config;

    LbGoto destination;

    SslFactory *ssl_factory = nullptr;

    const Logger logger;

public:
    LbListener(LbInstance &_instance,
               const LbListenerConfig &_config);
    ~LbListener();

    void Setup();
    void Scan(LbGotoMap &goto_map);

    unsigned FlushSSLSessionCache(long tm);

protected:
    void OnAccept(UniqueSocketDescriptor &&fd, SocketAddress address) override;
    void OnAcceptError(std::exception_ptr ep) override;
};

#endif
