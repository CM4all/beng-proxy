/*
 * OO representation of a struct sockaddr.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef SOCKET_ADDRESS_HXX
#define SOCKET_ADDRESS_HXX

#include <sys/socket.h>

struct sockaddr;
class Error;

class SocketAddress {
    const struct sockaddr *address;
    socklen_t size;

public:
    SocketAddress() = default;

    constexpr SocketAddress(const struct sockaddr *_address, socklen_t _size)
        :address(_address), size(_size) {}

    operator const struct sockaddr *() const {
        return address;
    }

    constexpr socklen_t GetSize() const {
        return size;
    }

    int GetFamily() const {
        return address->sa_family;
    }
};

#endif
