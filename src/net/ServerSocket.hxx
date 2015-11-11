/*
 * Listener on a TCP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef SERVER_SOCKET_HXX
#define SERVER_SOCKET_HXX

#include "SocketDescriptor.hxx"
#include "event/Event.hxx"

#include <utility>

class SocketAddress;
class Error;

class ServerSocket {
    SocketDescriptor fd;
    Event event;

public:
    ~ServerSocket();

    bool Listen(int family, int socktype, int protocol,
                SocketAddress address, Error &error);

    bool ListenTCP(unsigned port, Error &error);

    bool ListenPath(const char *path, Error &error);

    void AddEvent() {
        event.Add();
    }

    void RemoveEvent() {
        event.Delete();
    }

protected:
    /**
     * A new incoming connection has been established.
     *
     * @param fd the socket owned by the callee
     */
    virtual void OnAccept(SocketDescriptor &&fd, SocketAddress address) = 0;
    virtual void OnAcceptError(Error &&error) = 0;

private:
    void EventCallback();
};

#endif
