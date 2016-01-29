/*
 * Listener on a UDP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "udp_listener.hxx"
#include "system/fd_util.h"
#include "net/AllocatedSocketAddress.hxx"
#include "net/Parser.hxx"
#include "event/Event.hxx"
#include "event/Callback.hxx"
#include "system/Error.hxx"
#include "util/Error.hxx"
#include "util/Domain.hxx"

#include <socket/address.h>

#include <assert.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static constexpr Domain udp_listener_domain("udp_listener");

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

    void Join4(const struct in_addr *group);

    bool Reply(SocketAddress address,
               const void *data, size_t data_length,
               Error &error_r);

private:
    void EventCallback();
};

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
        Error error;
        error.SetErrno("recv() failed");
        handler.OnUdpError(std::move(error));
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
                 UdpHandler &handler)
{
    int fd = socket_cloexec_nonblock(address.GetFamily(),
                                     SOCK_DGRAM, 0);
    if (fd < 0)
        throw MakeErrno("Failed to create socket");

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
        const int e = errno;

        char buffer[256];
        const char *address_string =
            socket_address_to_string(buffer, sizeof(buffer),
                                     address.GetAddress(), address.GetSize())
            ? buffer
            : "?";

        close(fd);
        throw FormatErrno(e, "Failed to bind to %s", address_string);
    }

    return new UdpListener(fd, handler);
}

UdpListener *
udp_listener_port_new(const char *host_and_port, int default_port,
                      UdpHandler &handler)
{
    assert(host_and_port != nullptr);

    // TODO: migrate ParseSocketAddress() to exception
    Error error;
    auto address = ParseSocketAddress(host_and_port, default_port, true,
                                      error);
    if (address.IsNull())
        throw std::runtime_error(error.GetMessage());

    return udp_listener_new(address, handler);
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

inline void
UdpListener::Join4(const struct in_addr *group)
{
    struct ip_mreq r;
    r.imr_multiaddr = *group;
    r.imr_interface.s_addr = INADDR_ANY;

    if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &r, sizeof(r)) < 0)
        throw MakeErrno("Failed to join multicast group");
}

void
udp_listener_join4(UdpListener *udp, const struct in_addr *group)
{
    udp->Join4(group);
}

inline bool
UdpListener::Reply(SocketAddress address,
                   const void *data, size_t data_length,
                   Error &error_r)
{
    assert(fd >= 0);

    ssize_t nbytes = sendto(fd, data, data_length,
                            MSG_DONTWAIT|MSG_NOSIGNAL,
                            address.GetAddress(), address.GetSize());
    if (gcc_unlikely(nbytes < 0)) {
        error_r.SetErrno("Failed to send UDP packet");
        return false;
    }

    if ((size_t)nbytes != data_length) {
        error_r.Set(udp_listener_domain, "Short send");
        return false;
    }

    return true;
}

bool
udp_listener_reply(UdpListener *udp,
                   SocketAddress address,
                   const void *data, size_t data_length,
                   Error &error_r)
{
    assert(udp != nullptr);

    return udp->Reply(address, data, data_length, error_r);
}
