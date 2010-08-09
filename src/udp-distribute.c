/*
 * Distribute UDP (control) packets to all workers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "udp-distribute.h"
#include "fd_util.h"

#include <inline/list.h>

#include <glib.h>
#include <event.h>
#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>

struct udp_recipient {
    struct list_head siblings;

    pool_t pool;

    int fd;
    struct event event;
};

struct udp_distribute {
    pool_t pool;
    struct list_head recipients;
};

static void
udp_recipient_remove(struct udp_recipient *ur)
{
    list_remove(&ur->siblings);
    event_del(&ur->event);
    close(ur->fd);
    p_free(ur->pool, ur);
}

static void
udp_recipient_event_callback(G_GNUC_UNUSED int fd, G_GNUC_UNUSED short event,
                             void *ctx)
{
    struct udp_recipient *ur = ctx;

    assert(fd == ur->fd);

    udp_recipient_remove(ur);
}

struct udp_distribute *
udp_distribute_new(pool_t pool)
{
    struct udp_distribute *ud = p_malloc(pool, sizeof(*ud));
    ud->pool = pool;
    list_init(&ud->recipients);
    return ud;
}

void
udp_distribute_free(struct udp_distribute *ud)
{
    udp_distribute_clear(ud);
    p_free(ud->pool, ud);
}

void
udp_distribute_clear(struct udp_distribute *ud)
{
    while (!list_empty(&ud->recipients)) {
        struct udp_recipient *ur =
            (struct udp_recipient *)ud->recipients.next;
        udp_recipient_remove(ur);
    }
}

int
udp_distribute_add(struct udp_distribute *ud)
{
    int fds[2];
    if (socketpair_cloexec(AF_UNIX, SOCK_DGRAM, 0, fds) < 0)
        return -1;

    struct udp_recipient *ur = p_malloc(ud->pool, sizeof(*ur));
    ur->pool = ud->pool;
    ur->fd = fds[0];
    event_set(&ur->event, fds[0], EV_READ, udp_recipient_event_callback, ur);
    event_add(&ur->event, NULL);

    list_add(&ur->siblings, &ud->recipients);

    return fds[1];
}

void
udp_distribute_packet(struct udp_distribute *ud,
                      const void *payload, size_t payload_length)
{
    for (struct udp_recipient *ur = (struct udp_recipient *)ud->recipients.next;
         &ur->siblings != &ud->recipients;
         ur = (struct udp_recipient *)ur->siblings.next)
        send(ur->fd, payload, payload_length, MSG_DONTWAIT|MSG_NOSIGNAL);
}
