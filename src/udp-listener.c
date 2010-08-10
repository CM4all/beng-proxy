/*
 * Listener on a UDP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "udp-listener.h"
#include "fd_util.h"

#include <daemon/log.h>
#include <socket/resolver.h>

#include <glib.h>
#include <event.h>

#include <assert.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>

struct udp_listener {
    int fd;
    struct event event;

    udp_callback_t callback;
    void *callback_ctx;
};

static void
udp_listener_event_callback(int fd, G_GNUC_UNUSED short event, void *ctx)
{
    struct udp_listener *udp = ctx;
    struct sockaddr_storage sa;
    socklen_t sa_len;
    char buffer[4096];
    ssize_t nbytes;

    sa_len = sizeof(sa);
    nbytes = recvfrom(fd, buffer, sizeof(buffer), MSG_DONTWAIT,
                      (struct sockaddr *)&sa, &sa_len);
    if (nbytes < 0) {
        daemon_log(1, "recv() failed: %s\n", strerror(errno));
        return;
    }

    udp->callback(buffer, nbytes,
                  (struct sockaddr *)&sa, sa_len,
                  udp->callback_ctx);
}

struct udp_listener *
udp_listener_port_new(pool_t pool, const char *host_and_port, int default_port,
                      udp_callback_t callback, void *ctx)
{
    struct udp_listener *udp;
    int ret;
    struct addrinfo hints, *ai;

    assert(host_and_port != NULL);

    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_PASSIVE;
    hints.ai_socktype = SOCK_DGRAM;

    ret = socket_resolve_host_port(host_and_port, default_port, &hints, &ai);
    if (ret != 0) {
        daemon_log(1, "Failed to resolve %s: %s\n",
                   host_and_port, gai_strerror(ret));
        return NULL;
    }

    udp = p_malloc(pool, sizeof(*udp));
    udp->fd = socket_cloexec_nonblock(ai->ai_family, ai->ai_socktype,
                                      ai->ai_protocol);
    if (udp->fd < 0) {
        daemon_log(1, "Failed to create socket: %s\n",
                   strerror(errno));
        freeaddrinfo(ai);
        return NULL;
    }

    ret = bind(udp->fd, ai->ai_addr, ai->ai_addrlen);
    if (ret < 0) {
        daemon_log(1, "Failed to bind to %s: %s\n",
                   host_and_port, strerror(errno));
        close(udp->fd);
        freeaddrinfo(ai);
        return NULL;
    }

    freeaddrinfo(ai);

    event_set(&udp->event, udp->fd,
              EV_READ|EV_PERSIST, udp_listener_event_callback, udp);
    event_add(&udp->event, NULL);

    udp->callback = callback;
    udp->callback_ctx = ctx;

    return udp;
}

void
udp_listener_free(struct udp_listener *udp)
{
    assert(udp != NULL);
    assert(udp->fd >= 0);

    event_del(&udp->event);
    close(udp->fd);
}

void
udp_listener_set_fd(struct udp_listener *udp, int fd)
{
    assert(udp != NULL);
    assert(udp->fd >= 0);
    assert(fd >= 0);
    assert(udp->fd != fd);

    close(udp->fd);
    udp->fd = fd;
}

bool
udp_listener_join4(struct udp_listener *udp, const struct in_addr *group)
{
    struct ip_mreq r;
    int ret;

    r.imr_multiaddr = *group;
    r.imr_interface.s_addr = INADDR_ANY;

    ret = setsockopt(udp->fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &r, sizeof(r));
    if (ret < 0) {
        daemon_log(1, "Failed to join multicast group: %s\n", strerror(errno));
        return false;
    }

    return true;
}
