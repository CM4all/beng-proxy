/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef SOCKET_DESCRIPTOR_SOCKET_HXX
#define SOCKET_DESCRIPTOR_SOCKET_HXX

#include <inline/compiler.h>

#include <assert.h>
#include <stddef.h>

struct sockaddr;
class Error;
class SocketAddress;
class StaticSocketAddress;

/**
 * Wrapper for a socket file descriptor.
 */
class SocketDescriptor {
    int fd;

public:
    SocketDescriptor():fd(-1) {}

    explicit SocketDescriptor(int _fd):fd(_fd) {
        assert(fd >= 0);
    }

    SocketDescriptor(SocketDescriptor &&other):fd(other.fd) {
        other.fd = -1;
    }

    ~SocketDescriptor();

    bool IsDefined() const {
        return fd >= 0;
    }

    int Get() const {
        assert(IsDefined());

        return fd;
    }

    bool operator==(const SocketDescriptor &other) const {
        return fd == other.fd;
    }

    void Close();

    int Steal() {
        assert(IsDefined());

        int result = fd;
        fd = -1;
        return result;
    }

    /**
     * @return false on error (with errno set)
     */
    bool Create(int domain, int type, int protocol);

    bool Create(int domain, int type, int protocol, Error &error);
    bool CreateListen(int family, int socktype, int protocol,
                      const SocketAddress &address, Error &error);

    SocketDescriptor Accept(StaticSocketAddress &address, Error &error) const;

    /**
     * @return false on error (with errno set)
     */
    bool Connect(const SocketAddress address);

    gcc_pure
    StaticSocketAddress GetLocalAddress() const;
};

#endif
