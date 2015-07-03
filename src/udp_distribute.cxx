/*
 * Distribute UDP (control) packets to all workers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "udp_distribute.hxx"
#include "event/Callback.hxx"
#include "fd_util.h"
#include "pool.hxx"

#include <inline/list.h>

#include <event.h>
#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>

struct UdpRecipient {
    struct list_head siblings;

    struct pool *const pool;

    const int fd;
    struct event event;

    UdpRecipient(struct pool *_pool, int _fd)
        :pool(_pool), fd(_fd) {
        event_set(&event, fd, EV_READ,
                  MakeSimpleEventCallback(UdpRecipient, EventCallback),
                  this);
        event_add(&event, nullptr);
    }

    ~UdpRecipient() {
        event_del(&event);
        close(fd);
    }

    void RemoveAndDestroy() {
        list_remove(&siblings);
        DeleteFromPool(*pool, this);
    }

    void EventCallback() {
        RemoveAndDestroy();
    }
};

struct UdpDistribute {
    struct pool *const pool;
    struct list_head recipients;

    explicit UdpDistribute(struct pool *_pool)
        :pool(_pool) {
        list_init(&recipients);
    }

    ~UdpDistribute() {
        Clear();
    }

    int Add();
    void Clear();

    void Packet(const void *payload, size_t payload_length);
};

UdpDistribute *
udp_distribute_new(struct pool *pool)
{
    return NewFromPool<UdpDistribute>(*pool, pool);
}

void
udp_distribute_free(UdpDistribute *ud)
{
    DeleteFromPool(*ud->pool, ud);
}

void
UdpDistribute::Clear()
{
    while (!list_empty(&recipients)) {
        auto *ur = (UdpRecipient *)recipients.next;
        ur->RemoveAndDestroy();
    }
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

    auto *ur = NewFromPool<UdpRecipient>(*pool, pool, fds[0]);

    list_add(&ur->siblings, &recipients);

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
    for (auto *ur = (UdpRecipient *)recipients.next;
         &ur->siblings != &recipients;
         ur = (UdpRecipient *)ur->siblings.next)
        send(ur->fd, payload, payload_length, MSG_DONTWAIT|MSG_NOSIGNAL);
}

void
udp_distribute_packet(UdpDistribute *ud,
                      const void *payload, size_t payload_length)
{
    ud->Packet(payload, payload_length);
}
