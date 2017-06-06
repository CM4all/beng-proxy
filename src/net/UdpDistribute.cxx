/*
 * Distribute UDP (control) packets to all workers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "UdpDistribute.hxx"
#include "system/Error.hxx"
#include "util/DeleteDisposer.hxx"

#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>

UdpDistribute::Recipient::Recipient(EventLoop &_event_loop,
                                    UniqueFileDescriptor &&_fd)
    :fd(std::move(_fd)),
     event(_event_loop, fd.Get(), SocketEvent::READ,
           BIND_THIS_METHOD(EventCallback))
{
    event.Add();
}

UdpDistribute::Recipient::~Recipient()
{
    event.Delete();
}

void
UdpDistribute::Clear()
{
    recipients.clear_and_dispose(DeleteDisposer());
}

UniqueFileDescriptor
UdpDistribute::Add()
{
    UniqueFileDescriptor result_fd, recipient_fd;
    if (!UniqueFileDescriptor::CreateSocketPair(AF_LOCAL, SOCK_DGRAM, 0,
                                                result_fd, recipient_fd))
        throw MakeErrno("socketpair() failed");

    auto *ur = new Recipient(event_loop, std::move(recipient_fd));
    recipients.push_back(*ur);
    return result_fd;
}

void
UdpDistribute::Packet(const void *payload, size_t payload_length)
{
    for (auto &ur : recipients)
        send(ur.fd.Get(), payload, payload_length, MSG_DONTWAIT|MSG_NOSIGNAL);
}
