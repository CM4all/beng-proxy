/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef STATIC_SOCKET_ADDRESS_HXX
#define STATIC_SOCKET_ADDRESS_HXX

#include "SocketAddress.hxx"

#include <stddef.h>

struct sockaddr;
class Error;

class StaticSocketAddress {
    friend class SocketDescriptor;

    socklen_t size;
    struct sockaddr_storage address;

public:
    StaticSocketAddress() = default;

    constexpr size_t GetCapacity() const {
        return sizeof(address);
    }

    constexpr size_t GetSize() const {
        return size;
    }

    operator SocketAddress() const {
        return SocketAddress(reinterpret_cast<const struct sockaddr *>(&address),
                             size);
    }

    operator struct sockaddr *() {
        return reinterpret_cast<struct sockaddr *>(&address);
    }

    operator const struct sockaddr *() const {
        return reinterpret_cast<const struct sockaddr *>(&address);
    }

    int GetFamily() const {
        return address.ss_family;
    }

    bool IsDefined() const {
        return GetFamily() != AF_UNSPEC;
    }

    void Clear() {
        size = 0;
        address.ss_family = AF_UNSPEC;
    }

    /**
     * Make this a "local" address (UNIX domain socket).
     */
    void SetLocal(const char *path);

    bool Lookup(const char *host, int default_port, int socktype,
                Error &error);
};

#endif
