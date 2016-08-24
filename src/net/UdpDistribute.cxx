/*
 * Distribute UDP (control) packets to all workers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "UdpDistribute.hxx"
#include "event/SocketEvent.hxx"
#include "system/fd_util.h"
#include "util/DeleteDisposer.hxx"

#include <boost/intrusive/list.hpp>

#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>

class UdpDistribute {
    struct Recipient
        : boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::auto_unlink>> {

        const int fd;
        SocketEvent event;

        Recipient(EventLoop &event_loop, int _fd)
            :fd(_fd),
             event(event_loop, fd, EV_READ,
                   BIND_THIS_METHOD(EventCallback)) {
            event.Add();
        }

        ~Recipient() {
            event.Delete();
            close(fd);
        }

        void RemoveAndDestroy() {
            delete this;
        }

    private:
        void EventCallback(short) {
            RemoveAndDestroy();
        }
    };

    EventLoop &event_loop;

    boost::intrusive::list<Recipient,
                           boost::intrusive::constant_time_size<false>> recipients;

public:
    explicit UdpDistribute(EventLoop &_event_loop):event_loop(_event_loop) {}

    ~UdpDistribute() {
        Clear();
    }

    int Add();
    void Clear();

    void Packet(const void *payload, size_t payload_length);
};

UdpDistribute *
udp_distribute_new(EventLoop &event_loop)
{
    return new UdpDistribute(event_loop);
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

    auto *ur = new Recipient(event_loop, fds[0]);
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
