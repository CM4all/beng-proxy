/*
 * Create a listener socket for a child process.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CHILD_SOCKET_HXX
#define BENG_PROXY_CHILD_SOCKET_HXX

#include "glibfwd.hxx"

#include <sys/socket.h>
#include <sys/un.h>

struct ChildSocket {
    struct sockaddr_un address;
};

/**
 * @return the listener socket descriptor or -1 on error
 */
int
child_socket_create(ChildSocket *cs, int socket_type, GError **error_r);

void
child_socket_unlink(ChildSocket *cs);

static inline const struct sockaddr *
child_socket_address(const ChildSocket *cs)
{
    return (const struct sockaddr *)&cs->address;
}

static inline socklen_t
child_socket_address_length(const ChildSocket *cs)
{
    return SUN_LEN(&cs->address);
}

int
child_socket_connect(const ChildSocket *cs, GError **error_r);

#endif
