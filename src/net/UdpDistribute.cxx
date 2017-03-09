/*
 * Distribute UDP (control) packets to all workers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "UdpDistribute.hxx"
#include "system/fd_util.h"
#include "system/Error.hxx"
#include "util/DeleteDisposer.hxx"

#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>

UdpDistribute::Recipient::Recipient(EventLoop &_event_loop, int _fd)
    :fd(_fd),
     event(_event_loop, fd, EV_READ,
           BIND_THIS_METHOD(EventCallback))
{
    event.Add();
}

UdpDistribute::Recipient::~Recipient()
{
    event.Delete();
    close(fd);
}

void
UdpDistribute::Clear()
{
    recipients.clear_and_dispose(DeleteDisposer());
}

int
UdpDistribute::Add()
{
    int fds[2];
    if (socketpair_cloexec(AF_UNIX, SOCK_DGRAM, 0, fds) < 0)
        throw MakeErrno("socketpair() failed");

    auto *ur = new Recipient(event_loop, fds[0]);
    recipients.push_back(*ur);
    return fds[1];
}

void
UdpDistribute::Packet(const void *payload, size_t payload_length)
{
    for (auto &ur : recipients)
        send(ur.fd, payload, payload_length, MSG_DONTWAIT|MSG_NOSIGNAL);
}
