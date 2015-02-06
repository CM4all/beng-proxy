/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LISTENER_HXX
#define BENG_PROXY_LISTENER_HXX

#include "net/ServerSocket.hxx"

/**
 * Listener for incoming HTTP connections.
 */
class BPListener final : public ServerSocket {
    struct instance &instance;

    const char *const tag;

public:
    BPListener(struct instance &_instance, const char *_tag)
        :instance(_instance), tag(_tag) {}

protected:
    void OnAccept(SocketDescriptor &&fd, SocketAddress address) override;
    void OnAcceptError(Error &&error) override;
};

#endif
