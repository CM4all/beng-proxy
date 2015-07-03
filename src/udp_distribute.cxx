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
};

struct UdpDistribute {
    struct pool *pool;
    struct list_head recipients;
};

static void
udp_recipient_remove(UdpRecipient *ur)
{
    list_remove(&ur->siblings);
    event_del(&ur->event);
    close(ur->fd);
    DeleteFromPool(*ur->pool, ur);
}

static void
udp_recipient_event_callback(gcc_unused int fd, gcc_unused short event,
                             void *ctx)
{
    auto *ur = (UdpRecipient *)ctx;

    assert(fd == ur->fd);

    udp_recipient_remove(ur);
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
    udp_distribute_clear(ud);
    DeleteFromPool(*ud->pool, ud);
}

void
udp_distribute_clear(UdpDistribute *ud)
{
    while (!list_empty(&ud->recipients)) {
        UdpRecipient *ur =
            (UdpRecipient *)ud->recipients.next;
        udp_recipient_remove(ur);
    }
}

int
udp_distribute_add(UdpDistribute *ud)
{
    int fds[2];
    if (socketpair_cloexec(AF_UNIX, SOCK_DGRAM, 0, fds) < 0)
        return -1;

    auto *ur = NewFromPool<UdpRecipient>(*ud->pool);
    ur->pool = ud->pool;
    ur->fd = fds[0];
    event_set(&ur->event, fds[0], EV_READ, udp_recipient_event_callback, ur);
    event_add(&ur->event, nullptr);

    list_add(&ur->siblings, &ud->recipients);

    return fds[1];
}

void
udp_distribute_packet(UdpDistribute *ud,
                      const void *payload, size_t payload_length)
{
    for (UdpRecipient *ur = (UdpRecipient *)ud->recipients.next;
         &ur->siblings != &ud->recipients;
         ur = (UdpRecipient *)ur->siblings.next)
        send(ur->fd, payload, payload_length, MSG_DONTWAIT|MSG_NOSIGNAL);
}
