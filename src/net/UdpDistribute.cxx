/*
 * Distribute UDP (control) packets to all workers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "UdpDistribute.hxx"
#include "event/Event.hxx"
#include "event/Callback.hxx"
#include "system/fd_util.h"
#include "util/DeleteDisposer.hxx"

#include <boost/intrusive/list.hpp>

#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>

struct UdpRecipient
    : boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::auto_unlink>> {

    const int fd;
    Event event;

    UdpRecipient(int _fd)
        :fd(_fd) {
        event.Set(fd, EV_READ,
                  MakeSimpleEventCallback(UdpRecipient, EventCallback),
                  this);
        event.Add();
    }

    ~UdpRecipient() {
        event.Delete();
        close(fd);
    }

    void RemoveAndDestroy() {
        delete this;
    }

    void EventCallback() {
        RemoveAndDestroy();
    }
};

struct UdpDistribute {
    boost::intrusive::list<UdpRecipient,
                           boost::intrusive::constant_time_size<false>> recipients;

    ~UdpDistribute() {
        Clear();
    }

    int Add();
    void Clear();

    void Packet(const void *payload, size_t payload_length);
};

UdpDistribute *
udp_distribute_new()
{
    return new UdpDistribute();
}

void
udp_distribute_free(UdpDistribute *ud)
{
    delete ud;
}

void
UdpDistribute::Clear()
{
    recipients.clear_and_dispose(DeleteDisposer());
}

void
udp_distribute_clear(UdpDistribute *ud)
{
    ud->Clear();
}

inline int
UdpDistribute::Add()
{
    int fds[2];
    if (socketpair_cloexec(AF_UNIX, SOCK_DGRAM, 0, fds) < 0)
        return -1;

    auto *ur = new UdpRecipient(fds[0]);
    recipients.push_back(*ur);
    return fds[1];
}

int
udp_distribute_add(UdpDistribute *ud)
{
    return ud->Add();
}

inline void
UdpDistribute::Packet(const void *payload, size_t payload_length)
{
    for (auto &ur : recipients)
        send(ur.fd, payload, payload_length, MSG_DONTWAIT|MSG_NOSIGNAL);
}

void
udp_distribute_packet(UdpDistribute *ud,
                      const void *payload, size_t payload_length)
{
    ud->Packet(payload, payload_length);
}
