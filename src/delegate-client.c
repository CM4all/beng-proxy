/*
 * Fork a process and delegate open() to it.  The subprocess returns
 * the file descriptor over a unix socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "delegate-client.h"
#include "delegate-protocol.h"
#include "lease.h"

#ifdef __linux
#include <fcntl.h>
#endif

#include <daemon/log.h>

#include <event.h>

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <limits.h>
#include <sys/socket.h>

struct delegate_client {
    struct lease_ref lease_ref;
    int fd;
    struct event event;

    pool_t pool;
    const char *payload;
    size_t payload_rest;
    delegate_callback_t callback;
    void *callback_ctx;
};

/*
void
delegate_free(struct delegate *d)
{
    kill(d->pid, SIGTERM);
    close(d->fd);
}
*/

static void
delegate_try_read(struct delegate_client *d);

static void
delegate_read_event_callback(int fd __attr_unused, short event __attr_unused,
                              void *ctx)
{
    struct delegate_client *d = ctx;

    assert(d->fd == fd);
    assert(d->payload_rest == 0);

    delegate_try_read(d);
}

static void
delegate_try_read(struct delegate_client *d)
{
    struct iovec iov;
    int fd;
    char ccmsg[CMSG_SPACE(sizeof(fd))];
    struct cmsghdr *cmsg;
    struct msghdr msg = {
        .msg_name = NULL,
        .msg_namelen = 0,
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = ccmsg,
        .msg_controllen = sizeof(ccmsg),
    };
    struct delegate_header header;
    ssize_t nbytes;

    iov.iov_base = &header;
    iov.iov_len = sizeof(header);

    nbytes = recvmsg(d->fd, &msg, 0);
    if (nbytes < 0) {
        fd = -errno;

        lease_release(&d->lease_ref, false);

        daemon_log(1, "recvmsg() failed: %s\n", strerror(errno));
        d->callback(fd, d->callback_ctx);
        pool_unref(d->pool);
        return;
    }

    if ((size_t)nbytes != sizeof(header)) {
        lease_release(&d->lease_ref, false);

        daemon_log(1, "short recvmsg()\n");
        d->callback(-EWOULDBLOCK, d->callback_ctx);
        pool_unref(d->pool);
        return;
    }

    if (header.length != 0) {
        lease_release(&d->lease_ref, false);

        daemon_log(1, "Invalid message length\n");
        d->callback(-EINVAL, d->callback_ctx);
        pool_unref(d->pool);
        return;
    }

    if (header.command != 0) {
        /* i/o error */

        lease_release(&d->lease_ref, false);

        d->callback(-header.command, d->callback_ctx);
        pool_unref(d->pool);
        return;
    }

    cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg == NULL) {
        lease_release(&d->lease_ref, false);

        daemon_log(1, "No fd passed\n");
        d->callback(-EINVAL, d->callback_ctx);
        pool_unref(d->pool);
        return;
    }

    if (!cmsg->cmsg_type == SCM_RIGHTS) {
        lease_release(&d->lease_ref, false);

        daemon_log(1, "got control message of unknown type %d\n",
                   cmsg->cmsg_type);
        d->callback(-EINVAL, d->callback_ctx);
        pool_unref(d->pool);
        return;
    }

    lease_release(&d->lease_ref, true);

    fd = *(int*)CMSG_DATA(cmsg);
    d->callback(fd, d->callback_ctx);
    pool_unref(d->pool);
}

static void
delegate_try_write(struct delegate_client *d);

static void
delegate_write_event_callback(int fd __attr_unused, short event __attr_unused,
                              void *ctx)
{
    struct delegate_client *d = ctx;

    assert(d->fd == fd);
    assert(d->payload_rest > 0);

    delegate_try_write(d);
}

static void
delegate_try_write(struct delegate_client *d)
{
    ssize_t nbytes;

    nbytes = send(d->fd, d->payload, d->payload_rest, MSG_DONTWAIT);
    if (nbytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        int fd = -errno;

        lease_release(&d->lease_ref, false);

        daemon_log(1, "failed to send to delegate: %s\n", strerror(errno));
        d->callback(fd, d->callback_ctx);
        pool_unref(d->pool);
        return;
    }

    if (nbytes > 0) {
        d->payload += nbytes;
        d->payload_rest -= nbytes;
    }

    if (d->payload_rest == 0)
        event_set(&d->event, d->fd, EV_READ,
                  delegate_read_event_callback, d);

    event_add(&d->event, NULL);
}

void
delegate_open(int fd, const struct lease *lease, void *lease_ctx,
              pool_t pool, const char *path,
              delegate_callback_t callback, void *ctx)
{
    struct delegate_client *d;
    ssize_t nbytes;
    struct delegate_header header;

    d = p_malloc(pool, sizeof(*d));
    lease_ref_set(&d->lease_ref, lease, lease_ctx);
    d->fd = fd;
    d->pool = pool;

    header.length = strlen(path);
    header.command = 0;

    nbytes = send(d->fd, &header, sizeof(header), MSG_DONTWAIT);
    if (nbytes < 0) {
        fd = -errno;

        lease_release(&d->lease_ref, false);

        daemon_log(1, "failed to send to delegate: %s\n", strerror(errno));
        callback(fd, ctx);
        return;
    }

    if ((size_t)nbytes != sizeof(header)) {
        lease_release(&d->lease_ref, false);

        daemon_log(1, "short send to delegate\n");
        callback(-EWOULDBLOCK, ctx);
        return;
    }

    pool_ref(pool);

    d->payload = path;
    d->payload_rest = strlen(path);
    d->callback = callback;
    d->callback_ctx = ctx;

    event_set(&d->event, d->fd, EV_WRITE,
              delegate_write_event_callback, d);

    delegate_try_write(d);
}
