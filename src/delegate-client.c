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
#include "fd_util.h"
#include "pevent.h"

#ifdef __linux
#include <fcntl.h>
#endif

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

    const struct delegate_handler *handler;
    void *handler_ctx;

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
delegate_handle_fd(struct delegate_client *d, const struct msghdr *msg,
                   size_t length)
{
    if (length != 0) {
        delegate_release_socket(d, false);

        GError *error = g_error_new_literal(delegate_client_quark(), 0,
                                            "Invalid message length");
        d->handler->error(error, d->handler_ctx);
        pool_unref(d->pool);
        return;
    }

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(msg);
    if (cmsg == NULL) {
        delegate_release_socket(d, false);

        GError *error = g_error_new_literal(delegate_client_quark(), 0,
                                            "No fd passed");
        d->handler->error(error, d->handler_ctx);
        pool_unref(d->pool);
        return;
    }

    if (cmsg->cmsg_type != SCM_RIGHTS) {
        delegate_release_socket(d, false);

        GError *error = g_error_new(delegate_client_quark(), 0,
                                    "got control message of unknown type %d",
                                    cmsg->cmsg_type);
        d->handler->error(error, d->handler_ctx);
        pool_unref(d->pool);
        return;
    }

    delegate_release_socket(d, true);

    const int *fd_p = (const int *)CMSG_DATA(cmsg);

    int fd = *fd_p;
    d->handler->success(fd, d->handler_ctx);
    pool_unref(d->pool);
}

static void
delegate_handle_errno(struct delegate_client *d,
                      size_t length)
{
    int e;

    if (length != sizeof(e)) {
        delegate_release_socket(d, false);

        GError *error = g_error_new_literal(delegate_client_quark(), 0,
                                            "Invalid message length");
        d->handler->error(error, d->handler_ctx);
        pool_unref(d->pool);
        return;
    }

    ssize_t nbytes = recv(d->fd, &e, sizeof(e), 0);
    GError *error;

    if (nbytes == sizeof(e)) {
        delegate_release_socket(d, true);

        error = g_error_new_literal(g_file_error_quark(), e, g_strerror(e));
    } else {
        delegate_release_socket(d, false);

        error = g_error_new_literal(delegate_client_quark(), 0,
                                    "Failed to receive errno");
    }

    d->handler->error(error, d->handler_ctx);
    pool_unref(d->pool);
}

static void
delegate_handle_msghdr(struct delegate_client *d, const struct msghdr *msg,
                       enum delegate_response_command command, size_t length)
{
    switch (command) {
    case DELEGATE_FD:
        delegate_handle_fd(d, msg, length);
        return;

    case DELEGATE_ERRNO:
        /* i/o error */
        delegate_handle_errno(d, length);
        return;
    }

    delegate_release_socket(d, false);
    GError *error = g_error_new_literal(delegate_client_quark(), 0,
                                        "Invalid delegate response");
    d->handler->error(error, d->handler_ctx);
    pool_unref(d->pool);
}

static void
delegate_try_read(struct delegate_client *d)
{
    async_operation_finished(&d->operation);

    struct iovec iov;
    int fd;
    char ccmsg[CMSG_SPACE(sizeof(fd))];
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

    nbytes = recvmsg_cloexec(d->fd, &msg, 0);
    if (nbytes < 0) {
        fd = -errno;

        delegate_release_socket(d, false);

        GError *error = g_error_new(g_file_error_quark(), errno,
                                    "recvmsg() failed: %s\n",
                                    g_strerror(errno));
        d->handler->error(error, d->handler_ctx);
        pool_unref(d->pool);
        return;
    }

    if ((size_t)nbytes != sizeof(header)) {
        delegate_release_socket(d, false);

        GError *error = g_error_new_literal(delegate_client_quark(), 0,
                                            "short recvmsg()");
        d->handler->error(error, d->handler_ctx);
        pool_unref(d->pool);
        return;
    }

    delegate_handle_msghdr(d, &msg, header.command, header.length);
}

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
delegate_try_write(struct delegate_client *d)
{
    ssize_t nbytes;

    nbytes = send(d->fd, d->payload, d->payload_rest, MSG_DONTWAIT);
    if (nbytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        int e = errno;

        delegate_release_socket(d, false);

        GError *error = g_error_new(g_file_error_quark(), e,
                                    "failed to send to delegate: %s\n",
                                    g_strerror(e));
        d->handler->error(error, d->handler_ctx);
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

static void
delegate_write_event_callback(int fd __attr_unused, short event __attr_unused,
                              void *ctx)
{
    struct delegate_client *d = ctx;

    assert(d->fd == fd);
    assert(d->payload_rest > 0);

    delegate_try_write(d);
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
              const struct delegate_handler *handler, void *ctx,
              struct async_operation_ref *async_ref)
{
    struct delegate_client *d;
    ssize_t nbytes;

    d = p_malloc(pool, sizeof(*d));
    p_lease_ref_set(&d->lease_ref, lease, lease_ctx,
                    pool, "delegate_client_lease");
    d->fd = fd;
    d->pool = pool;

    struct delegate_header header = {
        .length = strlen(path),
        .command = DELEGATE_OPEN,
    };

    nbytes = send(d->fd, &header, sizeof(header), MSG_DONTWAIT);
    if (nbytes < 0) {
        int e = -errno;
        delegate_release_socket(d, false);

        GError *error = g_error_new(g_file_error_quark(), e,
                                    "failed to send to delegate: %s\n",
                                    g_strerror(e));
        handler->error(error, ctx);
        return;
    }

    if ((size_t)nbytes != sizeof(header)) {
        delegate_release_socket(d, false);

        GError *error = g_error_new_literal(delegate_client_quark(), 0,
                                            "short send to delegate");
        handler->error(error, ctx);
        return;
    }

    pool_ref(pool);

    d->payload = path;
    d->payload_rest = strlen(path);
    d->handler = handler;
    d->handler_ctx = ctx;

    async_init(&d->operation, &delegate_operation);
    async_ref_set(async_ref, &d->operation);

    event_set(&d->event, d->fd, EV_WRITE,
              delegate_write_event_callback, d);

    delegate_try_write(d);
}
