/*
 * Listener on a TCP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_LISTENER_H
#define BENG_PROXY_LB_LISTENER_H

#include "net/ServerSocket.hxx"

class Error;

class lb_listener final : public ServerSocket {
public:
    struct lb_instance &instance;

    const struct lb_listener_config &config;

    struct ssl_factory *ssl_factory = nullptr;

    lb_listener(struct lb_instance &_instance,
                const struct lb_listener_config &_config);
    ~lb_listener();

    bool Setup(Error &error);

protected:
    void OnAccept(SocketDescriptor &&fd, SocketAddress address) override;
    void OnAcceptError(Error &&error) override;
};

#endif
