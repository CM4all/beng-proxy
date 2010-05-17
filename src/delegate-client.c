/*
 * Fork a process and delegate open() to it.  The subprocess returns
 * the file descriptor over a unix socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "delegate-client.h"
#include "delegate-protocol.h"
#include "async.h"
#include "please.h"
#include "fd-util.h"
#include "pevent.h"

#ifdef __linux
#include <fcntl.h>
#endif

#include <daemon/log.h>

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

    struct async_operation operation;
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
delegate_release_socket(struct delegate_client *d, bool reuse)
{
    assert(d != NULL);
    assert(d->fd >= 0);

    p_lease_release(&d->lease_ref, reuse, d->pool);
}

static void
delegate_try_read(struct delegate_client *d);

static void
delegate_read_event_callback(int fd __attr_unused, short event __attr_unused,
                              void *ctx)
{
    struct delegate_client *d = ctx;

    p_event_consumed(&d->event, d->pool);

    assert(d->fd == fd);
    assert(d->payload_rest == 0);

    delegate_try_read(d);
}

static void
delegate_try_read(struct delegate_client *d)
{
    async_poison(&d->operation);

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

        delegate_release_socket(d, false);

        daemon_log(1, "recvmsg() failed: %s\n", strerror(errno));
        d->callback(fd, d->callback_ctx);
        pool_unref(d->pool);
        return;
    }

    if ((size_t)nbytes != sizeof(header)) {
        delegate_release_socket(d, false);

        daemon_log(1, "short recvmsg()\n");
        d->callback(-EWOULDBLOCK, d->callback_ctx);
        pool_unref(d->pool);
        return;
    }

    if (header.length != 0) {
        delegate_release_socket(d, false);

        daemon_log(1, "Invalid message length\n");
        d->callback(-EINVAL, d->callback_ctx);
        pool_unref(d->pool);
        return;
    }

    if (header.command != 0) {
        /* i/o error */

        delegate_release_socket(d, true);

        d->callback(-header.command, d->callback_ctx);
        pool_unref(d->pool);
        return;
    }

    cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg == NULL) {
        delegate_release_socket(d, false);

        daemon_log(1, "No fd passed\n");
        d->callback(-EINVAL, d->callback_ctx);
        pool_unref(d->pool);
        return;
    }

    if (!cmsg->cmsg_type == SCM_RIGHTS) {
        delegate_release_socket(d, false);

        daemon_log(1, "got control message of unknown type %d\n",
                   cmsg->cmsg_type);
        d->callback(-EINVAL, d->callback_ctx);
        pool_unref(d->pool);
        return;
    }

    delegate_release_socket(d, true);

    const int *fd_p = (const int *)CMSG_DATA(cmsg);
    fd = *fd_p;
    fd_set_cloexec(fd);
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

        delegate_release_socket(d, false);

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

    p_event_add(&d->event, NULL, d->pool, "delegate_client_event");
}


/*
 * async operation
 *
 */

static struct delegate_client *
async_to_delegate_client(struct async_operation *ao)
{
    return (struct delegate_client*)(((char*)ao) - offsetof(struct delegate_client, operation));
}

static void
delegate_connection_abort(struct async_operation *ao)
{
    struct delegate_client *d = async_to_delegate_client(ao);

    p_event_del(&d->event, d->pool);
    delegate_release_socket(d, false);
    pool_unref(d->pool);
}

static const struct async_operation_class delegate_operation = {
    .abort = delegate_connection_abort,
};


/*
 * constructor
 *
 */

void
delegate_open(int fd, const struct lease *lease, void *lease_ctx,
              pool_t pool, const char *path,
              delegate_callback_t callback, void *ctx,
              struct async_operation_ref *async_ref)
{
    struct delegate_client *d;
    ssize_t nbytes;
    struct delegate_header header;

    d = p_malloc(pool, sizeof(*d));
    p_lease_ref_set(&d->lease_ref, lease, lease_ctx,
                    pool, "delegate_client_lease");
    d->fd = fd;
    d->pool = pool;

    header.length = strlen(path);
    header.command = 0;

    nbytes = send(d->fd, &header, sizeof(header), MSG_DONTWAIT);
    if (nbytes < 0) {
        fd = -errno;

        delegate_release_socket(d, false);

        daemon_log(1, "failed to send to delegate: %s\n", strerror(errno));
        callback(fd, ctx);
        return;
    }

    if ((size_t)nbytes != sizeof(header)) {
        delegate_release_socket(d, false);

        daemon_log(1, "short send to delegate\n");
        callback(-EWOULDBLOCK, ctx);
        return;
    }

    pool_ref(pool);

    d->payload = path;
    d->payload_rest = strlen(path);
    d->callback = callback;
    d->callback_ctx = ctx;

    async_init(&d->operation, &delegate_operation);
    async_ref_set(async_ref, &d->operation);

    event_set(&d->event, d->fd, EV_WRITE,
              delegate_write_event_callback, d);

    delegate_try_write(d);
}
