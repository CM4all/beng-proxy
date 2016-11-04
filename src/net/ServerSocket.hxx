/*
 * Listener on a TCP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef SERVER_SOCKET_HXX
#define SERVER_SOCKET_HXX

#include "SocketDescriptor.hxx"
#include "event/SocketEvent.hxx"

#include <exception>

class SocketAddress;

class ServerSocket {
    SocketDescriptor fd;
    SocketEvent event;

public:
    explicit ServerSocket(EventLoop &event_loop)
        :event(event_loop, BIND_THIS_METHOD(EventCallback)) {}

    ~ServerSocket();

    /**
     * Throws std::runtime_error on error.
     */
    void Listen(int family, int socktype, int protocol,
                SocketAddress address,
                bool reuse_port,
                const char *bind_to_device);

    void ListenTCP(unsigned port);
    void ListenTCP4(unsigned port);
    void ListenTCP6(unsigned port);

    void ListenPath(const char *path);

    StaticSocketAddress GetLocalAddress() const;

    bool SetTcpDeferAccept(const int &seconds) {
        return fd.SetTcpDeferAccept(seconds);
    }

    bool SetBindToDevice(const char *name) {
        return fd.SetBindToDevice(name);
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
    virtual void OnAcceptError(std::exception_ptr ep) = 0;

private:
    void EventCallback(short events);
};

#endif
