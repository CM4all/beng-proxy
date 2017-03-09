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
    EventLoop &event_loop;

    TrafoHandler &handler;

    std::list<TrafoConnection> connections;

public:
    TrafoListener(EventLoop &_event_loop, TrafoHandler &_handler)
        :ServerSocket(_event_loop), event_loop(_event_loop),
         handler(_handler) {}

    using ServerSocket::ListenPath;

    void RemoveConnection(TrafoConnection &connection);

private:
    void OnAccept(UniqueSocketDescriptor &&fd, SocketAddress address) override;
    void OnAcceptError(std::exception_ptr ep) override;
};

#endif
