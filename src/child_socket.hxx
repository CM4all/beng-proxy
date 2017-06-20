/*
 * Create a listener socket for a child process.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CHILD_SOCKET_HXX
#define BENG_PROXY_CHILD_SOCKET_HXX

#include "net/SocketAddress.hxx"

#include <sys/socket.h>
#include <sys/un.h>

class SocketDescriptor;
class UniqueFileDescriptor;

struct ChildSocket {
    struct sockaddr_un address;

    ChildSocket() {
        address.sun_family = AF_UNSPEC;
    }

    bool IsDefined() const {
        return GetAddress().IsDefined();
    }

    /**
     * Throws std::runtime_error on error.
     */
    UniqueFileDescriptor Create(int socket_type);

    void Unlink();

    SocketAddress GetAddress() const {
        return SocketAddress((const struct sockaddr *)&address,
                             SUN_LEN(&address));
    }

    /**
     * Throws std::runtime_error on error.
     */
    SocketDescriptor Connect() const;
};

#endif
