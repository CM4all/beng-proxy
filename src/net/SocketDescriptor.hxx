/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef SOCKET_DESCRIPTOR_SOCKET_HXX
#define SOCKET_DESCRIPTOR_SOCKET_HXX

#include <assert.h>
#include <stddef.h>

struct sockaddr;
class Error;
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

    bool Create(int domain, int type, int protocol, Error &error);
    bool CreateListen(int family, int socktype, int protocol,
                      const struct sockaddr *address, size_t size,
                      Error &error);
    bool CreateListen(const StaticSocketAddress &address, Error &error);

    SocketDescriptor Accept(StaticSocketAddress &address, Error &error) const;
};

#endif
