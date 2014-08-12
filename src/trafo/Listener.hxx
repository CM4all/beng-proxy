/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef TRAFO_LISTENER_HXX
#define TRAFO_LISTENER_HXX

#include "Connection.hxx"
#include "net/ServerSocket.hxx"

#include <list>

class TrafoHandler;

class TrafoListener final : private ServerSocket {
    TrafoHandler &handler;

    std::list<TrafoConnection> connections;

public:
    TrafoListener(TrafoHandler &_handler)
        :handler(_handler) {}

    using ServerSocket::ListenPath;

    void RemoveConnection(TrafoConnection &connection);

private:
    void OnAccept(SocketDescriptor &&fd, SocketAddress address) override;
    void OnAcceptError(Error &&error) override;
};

#endif
