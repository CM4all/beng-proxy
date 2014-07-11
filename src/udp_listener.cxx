/*
 * Listener on a UDP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "udp_listener.hxx"
#include "fd_util.h"
#include "net/AllocatedSocketAddress.hxx"
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

G_GNUC_CONST
static inline GQuark
udp_listener_quark(void)
{
    return g_quark_from_static_string("udp_listener");
}

static void
udp_listener_event_callback(int fd, gcc_unused short event, void *ctx)
{
    struct udp_listener *udp = (struct udp_listener *)ctx;

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

#ifdef __clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-align"
#endif

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    while (cmsg != nullptr) {
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

#ifdef __clang__
#pragma GCC diagnostic pop
#endif

    udp->handler->datagram(buffer, nbytes,
                           SocketAddress((struct sockaddr *)&sa,
                                         msg.msg_namelen),
                           uid,
                           udp->handler_ctx);
}

struct udp_listener *
udp_listener_new(SocketAddress address,
                 const struct udp_handler *handler, void *ctx,
                 GError **error_r)
{
    assert(handler != nullptr);
    assert(handler->datagram != nullptr);
    assert(handler->error != nullptr);

    auto udp = new udp_listener();
    udp->fd = socket_cloexec_nonblock(address.GetFamily(),
                                      SOCK_DGRAM, 0);
    if (udp->fd < 0) {
        set_error_errno_msg(error_r, "Failed to create socket");
        return nullptr;
    }

    if (address.GetFamily() == AF_UNIX) {
        const struct sockaddr_un *sun = (const struct sockaddr_un *)
            (const struct sockaddr *)address;
        if (sun->sun_path[0] != '\0')
            /* delete non-abstract socket files before reusing them */
            unlink(sun->sun_path);

        /* we want to receive the client's UID */
        int value = 1;
        setsockopt(udp->fd, SOL_SOCKET, SO_PASSCRED, &value, sizeof(value));
    }

    if (bind(udp->fd, address, address.GetSize()) < 0) {
        char buffer[256];
        const char *address_string =
            socket_address_to_string(buffer, sizeof(buffer),
                                     address, address.GetSize())
            ? buffer
            : "?";

        g_set_error(error_r, errno_quark(), errno,
                    "Failed to bind to %s: %s",
                    address_string, strerror(errno));
        close(udp->fd);
        return nullptr;
    }

    event_set(&udp->event, udp->fd,
              EV_READ|EV_PERSIST, udp_listener_event_callback, udp);
    event_add(&udp->event, nullptr);

    udp->handler = handler;
    udp->handler_ctx = ctx;

    return udp;
}

struct udp_listener *
udp_listener_port_new(const char *host_and_port, int default_port,
                      const struct udp_handler *handler, void *ctx,
                      GError **error_r)
{
    assert(host_and_port != nullptr);
    assert(handler != nullptr);
    assert(handler->datagram != nullptr);
    assert(handler->error != nullptr);

    AllocatedSocketAddress address;
    if (!address.Parse(host_and_port, default_port, true, error_r))
        return nullptr;

    return udp_listener_new(address,
                            handler, ctx, error_r);
}

void
udp_listener_free(struct udp_listener *udp)
{
    assert(udp != nullptr);
    assert(udp->fd >= 0);

    event_del(&udp->event);
    close(udp->fd);
}

void
udp_listener_enable(struct udp_listener *udp)
{
    assert(udp != nullptr);
    assert(udp->fd >= 0);

    event_add(&udp->event, nullptr);
}

void
udp_listener_disable(struct udp_listener *udp)
{
    assert(udp != nullptr);
    assert(udp->fd >= 0);

    event_del(&udp->event);
}

void
udp_listener_set_fd(struct udp_listener *udp, int fd)
{
    assert(udp != nullptr);
    assert(udp->fd >= 0);
    assert(fd >= 0);
    assert(udp->fd != fd);

    event_del(&udp->event);

    close(udp->fd);
    udp->fd = fd;

    event_set(&udp->event, udp->fd,
              EV_READ|EV_PERSIST, udp_listener_event_callback, udp);
    event_add(&udp->event, nullptr);
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
                   SocketAddress address,
                   const void *data, size_t data_length,
                   GError **error_r)
{
    assert(udp != nullptr);
    assert(udp->fd >= 0);

    ssize_t nbytes = sendto(udp->fd, data, data_length,
                            MSG_DONTWAIT|MSG_NOSIGNAL,
                            address, address.GetSize());
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
