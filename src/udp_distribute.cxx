/*
 * Distribute UDP (control) packets to all workers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "udp_distribute.hxx"
#include "fd_util.h"
#include "pool.hxx"

#include <inline/list.h>

#include <event.h>
#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>

struct UdpRecipient {
    struct list_head siblings;

    struct pool *pool;

    int fd;
    struct event event;

    void RemoveAndDestroy() {
        list_remove(&siblings);
        event_del(&event);
        close(fd);
        DeleteFromPool(*pool, this);
    }
};

struct UdpDistribute {
    struct pool *pool;
    struct list_head recipients;

    int Add();
    void Clear();

    void Packet(const void *payload, size_t payload_length);
};

static void
udp_recipient_event_callback(gcc_unused int fd, gcc_unused short event,
                             void *ctx)
{
    auto *ur = (UdpRecipient *)ctx;

    assert(fd == ur->fd);

    ur->RemoveAndDestroy();
}

UdpDistribute *
udp_distribute_new(struct pool *pool)
{
    auto *ud = NewFromPool<UdpDistribute>(*pool);
    ud->pool = pool;
    list_init(&ud->recipients);
    return ud;
}

void
udp_distribute_free(UdpDistribute *ud)
{
    ud->Clear();
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

    auto *ur = NewFromPool<UdpRecipient>(*pool);
    ur->pool = pool;
    ur->fd = fds[0];
    event_set(&ur->event, fds[0], EV_READ, udp_recipient_event_callback, ur);
    event_add(&ur->event, nullptr);

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
