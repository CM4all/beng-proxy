/*
 * Listener on a UDP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "udp_listener.hxx"
#include "fd_util.h"
#include "net/AllocatedSocketAddress.hxx"
#include "event/Event.hxx"
#include "event/Callback.hxx"
#include "gerrno.h"

#include <socket/address.h>

#include <glib.h>

#include <assert.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

class UdpListener {
    int fd;
    Event event;

    UdpHandler &handler;

public:
    UdpListener(int _fd, UdpHandler &_handler)
        :fd(_fd), handler(_handler) {
        event.Set(fd, EV_READ|EV_PERSIST,
                  MakeSimpleEventCallback(UdpListener, EventCallback), this);
        event.Add();
    }

    ~UdpListener() {
        assert(fd >= 0);

        event.Delete();
        close(fd);
    }

    void Enable() {
        event.Add();
    }

    void Disable() {
        event.Delete();
    }

    void SetFd(int _fd) {
        assert(fd >= 0);
        assert(_fd >= 0);
        assert(fd != _fd);

        event.Delete();

        close(fd);
        fd = _fd;

        event.Set(fd, EV_READ|EV_PERSIST,
                  MakeSimpleEventCallback(UdpListener, EventCallback), this);
        event.Add();
    }

    bool Join4(const struct in_addr *group, GError **error_r);

    bool Reply(SocketAddress address,
               const void *data, size_t data_length,
               GError **error_r);

private:
    void EventCallback();
};

G_GNUC_CONST
static inline GQuark
udp_listener_quark(void)
{
    return g_quark_from_static_string("udp_listener");
}

inline void
UdpListener::EventCallback()
{
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
        handler.OnUdpError(error);
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

    handler.OnUdpDatagram(buffer, nbytes,
                          SocketAddress((struct sockaddr *)&sa,
                                        msg.msg_namelen),
                          uid);
}

UdpListener *
udp_listener_new(SocketAddress address,
                 UdpHandler &handler,
                 GError **error_r)
{
    int fd = socket_cloexec_nonblock(address.GetFamily(),
                                     SOCK_DGRAM, 0);
    if (fd < 0) {
        set_error_errno_msg(error_r, "Failed to create socket");
        return nullptr;
    }

    if (address.GetFamily() == AF_UNIX) {
        const struct sockaddr_un *sun = (const struct sockaddr_un *)
            address.GetAddress();
        if (sun->sun_path[0] != '\0')
            /* delete non-abstract socket files before reusing them */
            unlink(sun->sun_path);

        /* we want to receive the client's UID */
        int value = 1;
        setsockopt(fd, SOL_SOCKET, SO_PASSCRED, &value, sizeof(value));
    }

    if (bind(fd, address.GetAddress(), address.GetSize()) < 0) {
        char buffer[256];
        const char *address_string =
            socket_address_to_string(buffer, sizeof(buffer),
                                     address.GetAddress(), address.GetSize())
            ? buffer
            : "?";

        g_set_error(error_r, errno_quark(), errno,
                    "Failed to bind to %s: %s",
                    address_string, strerror(errno));
        close(fd);
        return nullptr;
    }

    return new UdpListener(fd, handler);
}

UdpListener *
udp_listener_port_new(const char *host_and_port, int default_port,
                      UdpHandler &handler,
                      GError **error_r)
{
    assert(host_and_port != nullptr);

    AllocatedSocketAddress address;
    if (!address.Parse(host_and_port, default_port, true, error_r))
        return nullptr;

    return udp_listener_new(address,
                            handler, error_r);
}

void
udp_listener_free(UdpListener *udp)
{
    assert(udp != nullptr);

    delete udp;
}

void
udp_listener_enable(UdpListener *udp)
{
    assert(udp != nullptr);

    udp->Enable();
}

void
udp_listener_disable(UdpListener *udp)
{
    assert(udp != nullptr);

    udp->Disable();
}

void
udp_listener_set_fd(UdpListener *udp, int fd)
{
    assert(udp != nullptr);

    udp->SetFd(fd);
}

inline bool
UdpListener::Join4(const struct in_addr *group, GError **error_r)
{
    struct ip_mreq r;
    r.imr_multiaddr = *group;
    r.imr_interface.s_addr = INADDR_ANY;

    if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &r, sizeof(r)) < 0) {
        set_error_errno_msg(error_r, "Failed to join multicast group");
        return false;
    }

    return true;
}

bool
udp_listener_join4(UdpListener *udp, const struct in_addr *group,
                   GError **error_r)
{
    return udp->Join4(group, error_r);
}

inline bool
UdpListener::Reply(SocketAddress address,
                   const void *data, size_t data_length,
                   GError **error_r)
{
    assert(fd >= 0);

    ssize_t nbytes = sendto(fd, data, data_length,
                            MSG_DONTWAIT|MSG_NOSIGNAL,
                            address.GetAddress(), address.GetSize());
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

bool
udp_listener_reply(UdpListener *udp,
                   SocketAddress address,
                   const void *data, size_t data_length,
                   GError **error_r)
{
    assert(udp != nullptr);

    return udp->Reply(address, data, data_length, error_r);
}
