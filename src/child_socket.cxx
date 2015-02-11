/*
 * Create a listener socket for a child process.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "child_socket.hxx"
#include "fd_util.h"
#include "gerrno.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

static bool
make_child_socket_path(struct sockaddr_un *address)
{
    address->sun_family = AF_UNIX;

    strcpy(address->sun_path, "/tmp/cm4all-beng-proxy-socket-XXXXXX");
    return *mktemp(address->sun_path) != 0;
}

int
child_socket_create(struct child_socket *cs, int socket_type, GError **error_r)
{
    if (!make_child_socket_path(&cs->address)) {
        set_error_errno_msg(error_r, "mktemp() failed");
        return -1;
    }

    unlink(cs->address.sun_path);

    const int fd = socket(PF_UNIX, socket_type, 0);
    if (fd < 0) {
        set_error_errno_msg(error_r, "failed to create local socket");
        return -1;
    }

    if (bind(fd, child_socket_address(cs),
             child_socket_address_length(cs)) < 0) {
        set_error_errno_msg(error_r, "failed to bind local socket");
        close(fd);
        return -1;
    }

    /* allow only beng-proxy to connect to it */
    chmod(cs->address.sun_path, 0600);

    if (listen(fd, 8) < 0) {
        set_error_errno_msg(error_r, "failed to listen on local socket");
        close(fd);
        return -1;
    }

    return fd;
}

void
child_socket_unlink(struct child_socket *cs)
{
    unlink(cs->address.sun_path);
}

int
child_socket_connect(const struct child_socket *cs, GError **error_r)
{
    int fd = socket_cloexec_nonblock(PF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        set_error_errno(error_r);
        return -1;
    }

    if (connect(fd, child_socket_address(cs),
                child_socket_address_length(cs)) < 0) {
        set_error_errno_msg(error_r, "connect failed");
        close(fd);
        return -1;
    }

    return fd;
}
