/*
 * Listener on a UDP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

/* for struct ucred */
#define _GNU_SOURCE 1

#include "udp-listener.h"
#include "fd_util.h"
#include "address_string.h"
#include "address_envelope.h"
#include "gerrno.h"

#include <socket/address.h>

#include <glib.h>
#include <event.h>

#include <assert.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <unistd.h>

struct udp_listener {
    int fd;
    struct event event;

    const struct udp_handler *handler;
    void *handler_ctx;
};

static void
udp_listener_event_callback(int fd, G_GNUC_UNUSED short event, void *ctx)
{
    struct udp_listener *udp = ctx;

    char buffer[4096];
    struct iovec iov;
    iov.iov_base = buffer;
    iov.iov_len = sizeof(buffer);

    struct sockaddr_storage sa;
    char cbuffer[CMSG_SPACE(1024)];
    struct msghdr msg = {
        .msg_name = &sa,
        .msg_namelen = sizeof(sa),
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = cbuffer,
        .msg_controllen = sizeof(cbuffer),
    };

    ssize_t nbytes = recvmsg_cloexec(fd, &msg, MSG_DONTWAIT);
    if (nbytes < 0) {
        GError *error = new_error_errno_msg("recv() failed");
        udp->handler->error(error, udp->handler_ctx);
        return;
    }

    int uid = -1;

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    while (cmsg != NULL) {
        if (cmsg->cmsg_level == SOL_SOCKET &&
            cmsg->cmsg_type == SCM_CREDENTIALS) {
            const struct ucred *cred = (const struct ucred *)CMSG_DATA(cmsg);
            uid = cred->uid;
        } else if (cmsg->cmsg_level == SOL_SOCKET &&
                   cmsg->cmsg_type == SCM_RIGHTS) {
            const int *fds = (const int *)CMSG_DATA(cmsg);
            const unsigned n = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(fds[0]);

            for (unsigned i = 0; i < n; ++i)
                close(fds[i]);
        }

        cmsg = CMSG_NXTHDR(&msg, cmsg);
    }

    udp->handler->datagram(buffer, nbytes,
                           (struct sockaddr *)&sa, msg.msg_namelen,
                           uid,
                           udp->handler_ctx);
}

struct udp_listener *
udp_listener_new(struct pool *pool,
                 const struct sockaddr *address, size_t address_length,
                 const struct udp_handler *handler, void *ctx,
                 GError **error_r)
{
    assert(address != NULL);
    assert(address_length > 0);
    assert(handler != NULL);
    assert(handler->datagram != NULL);
    assert(handler->error != NULL);

    struct udp_listener *udp = p_malloc(pool, sizeof(*udp));
    udp->fd = socket_cloexec_nonblock(address->sa_family,
                                      SOCK_DGRAM, 0);
    if (udp->fd < 0) {
        set_error_errno_msg(error_r, "Failed to create socket");
        return NULL;
    }

    if (address->sa_family == AF_UNIX) {
        const struct sockaddr_un *sun = (const struct sockaddr_un *)address;
        if (sun->sun_path[0] != '\0')
            /* delete non-abstract socket files before reusing them */
            unlink(sun->sun_path);

        /* we want to receive the client's UID */
        int value = 1;
        setsockopt(udp->fd, SOL_SOCKET, SO_PASSCRED, &value, sizeof(value));
    }

    if (bind(udp->fd, address, address_length) < 0) {
        char buffer[256];
        const char *address_string =
            socket_address_to_string(buffer, sizeof(buffer),
                                     address, address_length)
            ? buffer
            : "?";

        g_set_error(error_r, errno_quark(), errno,
                    "Failed to bind to %s: %s",
                    address_string, strerror(errno));
        close(udp->fd);
        return NULL;
    }

    event_set(&udp->event, udp->fd,
              EV_READ|EV_PERSIST, udp_listener_event_callback, udp);
    event_add(&udp->event, NULL);

    udp->handler = handler;
    udp->handler_ctx = ctx;

    return udp;
}

struct udp_listener *
udp_listener_port_new(struct pool *pool,
                      const char *host_and_port, int default_port,
                      const struct udp_handler *handler, void *ctx,
                      GError **error_r)
{
    assert(host_and_port != NULL);
    assert(handler != NULL);
    assert(handler->datagram != NULL);
    assert(handler->error != NULL);

    struct address_envelope *envelope =
        address_envelope_parse(pool, host_and_port, default_port,
                               true, error_r);
    if (envelope == NULL)
        return NULL;

    struct udp_listener *udp =
        udp_listener_new(pool, &envelope->address, envelope->length,
                         handler, ctx, error_r);
    p_free(pool, envelope);
    return udp;
}

void
udp_listener_free(struct udp_listener *udp)
{
    assert(udp != NULL);
    assert(udp->fd >= 0);

    event_del(&udp->event);
    close(udp->fd);
}

void
udp_listener_enable(struct udp_listener *udp)
{
    assert(udp != NULL);
    assert(udp->fd >= 0);

    event_add(&udp->event, NULL);
}

void
udp_listener_disable(struct udp_listener *udp)
{
    assert(udp != NULL);
    assert(udp->fd >= 0);

    event_del(&udp->event);
}

void
udp_listener_set_fd(struct udp_listener *udp, int fd)
{
    assert(udp != NULL);
    assert(udp->fd >= 0);
    assert(fd >= 0);
    assert(udp->fd != fd);

    event_del(&udp->event);

    close(udp->fd);
    udp->fd = fd;

    event_set(&udp->event, udp->fd,
              EV_READ|EV_PERSIST, udp_listener_event_callback, udp);
    event_add(&udp->event, NULL);
}

bool
udp_listener_join4(struct udp_listener *udp, const struct in_addr *group,
                   GError **error_r)
{
    struct ip_mreq r;
    r.imr_multiaddr = *group;
    r.imr_interface.s_addr = INADDR_ANY;

    if (setsockopt(udp->fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                   &r, sizeof(r)) < 0) {
        set_error_errno_msg(error_r, "Failed to join multicast group");
        return false;
    }

    return true;
}

bool
udp_listener_reply(struct udp_listener *udp,
                   const struct sockaddr *address, size_t address_length,
                   const void *data, size_t data_length,
                   GError **error_r)
{
    assert(udp != NULL);
    assert(udp->fd >= 0);
    assert(address != NULL);
    assert(address_length > 0);

    ssize_t nbytes = sendto(udp->fd, data, data_length,
                            MSG_DONTWAIT|MSG_NOSIGNAL,
                            address, address_length);
    if (G_UNLIKELY(nbytes < 0)) {
        set_error_errno_msg(error_r, "Failed to send UDP packet");
        return false;
    }

    if ((size_t)nbytes != data_length) {
        g_set_error(error_r, udp_listener_quark(), 0, "Short send");
        return false;
    }

    return true;
}
