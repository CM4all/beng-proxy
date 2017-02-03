/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef SOCKET_DESCRIPTOR_SOCKET_HXX
#define SOCKET_DESCRIPTOR_SOCKET_HXX

#include <inline/compiler.h>

#include <algorithm>

#include <assert.h>
#include <stddef.h>

class SocketAddress;
class StaticSocketAddress;

/**
 * Wrapper for a socket file descriptor.
 */
class SocketDescriptor {
    int fd = -1;

public:
    SocketDescriptor() = default;

    explicit SocketDescriptor(int _fd):fd(_fd) {
        assert(fd >= 0);
    }

    SocketDescriptor(SocketDescriptor &&other)
        :fd(std::exchange(other.fd, -1)) {}

    ~SocketDescriptor();

    SocketDescriptor &operator=(SocketDescriptor &&src) {
        std::swap(fd, src.fd);
        return *this;
    }

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

    bool Bind(SocketAddress address);

    bool SetOption(int level, int name, const void *value, size_t size);

    bool SetBoolOption(int level, int name, bool _value) {
        const int value = _value;
        return SetOption(level, name, &value, sizeof(value));
    }

    bool SetReuseAddress(bool value=true);
    bool SetReusePort(bool value=true);

    bool SetTcpDeferAccept(const int &seconds);
    bool SetV6Only(bool value);

    /**
     * Setter for SO_BINDTODEVICE.
     */
    bool SetBindToDevice(const char *name);

    bool SetTcpFastOpen(int qlen=16);

    /**
     * @return an "undefined" instance on error
     */
    SocketDescriptor Accept(StaticSocketAddress &address) const;

    /**
     * @return false on error (with errno set)
     */
    bool Connect(const SocketAddress address);

    int GetError();

    gcc_pure
    StaticSocketAddress GetLocalAddress() const;
};

#endif
