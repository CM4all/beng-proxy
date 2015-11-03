/*
 * Create a listener socket for a child process.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CHILD_SOCKET_HXX
#define BENG_PROXY_CHILD_SOCKET_HXX

#include "net/SocketAddress.hxx"
#include "glibfwd.hxx"

#include <sys/socket.h>
#include <sys/un.h>

struct ChildSocket {
    struct sockaddr_un address;

    /**
     * @return the listener socket descriptor or -1 on error
     */
    int Create(int socket_type, GError **error_r);

    void Unlink();

    SocketAddress GetAddress() const {
        return SocketAddress((const struct sockaddr *)&address,
                             SUN_LEN(&address));
    }

    int Connect(GError **error_r) const;
};

#endif
