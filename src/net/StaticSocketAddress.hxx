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

    typedef SocketAddress::size_type size_type;

    size_type size;
    struct sockaddr_storage address;

public:
    StaticSocketAddress() = default;

    StaticSocketAddress &operator=(const SocketAddress &src);

    constexpr size_t GetCapacity() const {
        return sizeof(address);
    }

    const struct sockaddr *GetAddress() const {
        return reinterpret_cast<const struct sockaddr *>(&address);
    }

    constexpr size_type GetSize() const {
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
     * Make this a "local" address (UNIX domain socket).  If the path
     * begins with a '@', then the rest specifies an "abstract" local
     * address.
     */
    void SetLocal(const char *path);
};

#endif
