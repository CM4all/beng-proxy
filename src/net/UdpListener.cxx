/*
 * Listener on a UDP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "UdpListener.hxx"
#include "UdpHandler.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "system/fd_util.h"
#include "system/Error.hxx"

#include <assert.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>

UdpListener::UdpListener(EventLoop &event_loop, UniqueSocketDescriptor &&_fd,
                         UdpHandler &_handler)
    :fd(std::move(_fd)),
     event(event_loop, fd.Get(), SocketEvent::READ|SocketEvent::PERSIST,
           BIND_THIS_METHOD(EventCallback)),
     handler(_handler)
{
    event.Add();
}

UdpListener::~UdpListener()
{
    assert(fd.IsDefined());

    event.Delete();
}

void
UdpListener::SetFd(UniqueSocketDescriptor &&_fd)
{
    assert(fd.IsDefined());
    assert(_fd.IsDefined());

    event.Delete();

    fd = std::move(_fd);

    event.Set(fd.Get(), SocketEvent::READ|SocketEvent::PERSIST);
    event.Add();
}

void
UdpListener::EventCallback(unsigned)
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

    ssize_t nbytes = recvmsg_cloexec(fd.Get(), &msg, MSG_DONTWAIT);
    if (nbytes < 0) {
        handler.OnUdpError(std::make_exception_ptr(MakeErrno("recv() failed")));
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

void
UdpListener::Reply(SocketAddress address,
                   const void *data, size_t data_length)
{
    assert(fd.IsDefined());

    ssize_t nbytes = sendto(fd.Get(), data, data_length,
                            MSG_DONTWAIT|MSG_NOSIGNAL,
                            address.GetAddress(), address.GetSize());
    if (gcc_unlikely(nbytes < 0))
        throw MakeErrno("Failed to send UDP packet");

    if ((size_t)nbytes != data_length)
        throw std::runtime_error("Short send");
}
