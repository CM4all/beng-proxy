/*
 * Listener on a TCP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef SERVER_SOCKET_HXX
#define SERVER_SOCKET_HXX

#include "SocketDescriptor.hxx"
#include "event/SocketEvent.hxx"

class SocketAddress;
class Error;

class ServerSocket {
    SocketDescriptor fd;
    SocketEvent event;

public:
    explicit ServerSocket(EventLoop &event_loop)
        :event(event_loop, BIND_THIS_METHOD(EventCallback)) {}

    ~ServerSocket();

    bool Listen(int family, int socktype, int protocol,
                SocketAddress address, Error &error);

    bool ListenTCP(unsigned port, Error &error);

    bool ListenPath(const char *path, Error &error);

    bool SetTcpDeferAccept(const int &seconds) {
        return fd.SetTcpDeferAccept(seconds);
    }

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
    void EventCallback(short events);
};

#endif
