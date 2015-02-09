/*
 * OO representation of a struct sockaddr.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef SOCKET_ADDRESS_HXX
#define SOCKET_ADDRESS_HXX

#include <cstddef>

#include <sys/socket.h>

struct sockaddr;
class Error;

class SocketAddress {
    const struct sockaddr *address;
    socklen_t size;

public:
    SocketAddress() = default;

    constexpr SocketAddress(std::nullptr_t):address(nullptr), size(0) {}

    constexpr SocketAddress(const struct sockaddr *_address, socklen_t _size)
        :address(_address), size(_size) {}

    static constexpr SocketAddress Null() {
        return {nullptr, 0};
    }

    bool IsNull() const {
        return address == nullptr;
    }

    operator const struct sockaddr *() const {
        return address;
    }

    const struct sockaddr *GetAddress() const {
        return address;
    }

    constexpr socklen_t GetSize() const {
        return size;
    }

    int GetFamily() const {
        return address->sa_family;
    }

    /**
     * Does the object have a well-defined address?  Check !IsNull()
     * before calling this method.
     */
    bool IsDefined() const {
        return GetFamily() != AF_UNSPEC;
    }

    bool operator==(SocketAddress other) const;
};

#endif
