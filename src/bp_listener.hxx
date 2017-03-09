/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LISTENER_HXX
#define BENG_PROXY_LISTENER_HXX

#include "net/ServerSocket.hxx"

struct BpInstance;

/**
 * Listener for incoming HTTP connections.
 */
class BPListener final : public ServerSocket {
    BpInstance &instance;

    const char *const tag;

public:
    BPListener(BpInstance &_instance, const char *_tag);

protected:
    void OnAccept(UniqueSocketDescriptor &&fd, SocketAddress address) override;
    void OnAcceptError(std::exception_ptr ep) override;
};

#endif
