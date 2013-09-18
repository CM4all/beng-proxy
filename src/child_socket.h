/*
 * Create a listener socket for a child process.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CHILD_SOCKET_H
#define BENG_PROXY_CHILD_SOCKET_H

#include <glib.h>

#include <sys/socket.h>
#include <sys/un.h>

struct child_socket {
    struct sockaddr_un address;
};

/**
 * @return the listener socket descriptor or -1 on error
 */
int
child_socket_create(struct child_socket *cs, GError **error_r);

void
child_socket_unlink(struct child_socket *cs);

static inline const struct sockaddr *
child_socket_address(const struct child_socket *cs)
{
    return (const struct sockaddr *)&cs->address;
}

static inline socklen_t
child_socket_address_length(const struct child_socket *cs)
{
    return SUN_LEN(&cs->address);
}

#endif
