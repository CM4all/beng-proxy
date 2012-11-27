/*
 * This istream implementation creates a socket pair with
 * socketpair().  It provides one side as istream/handler pair, and
 * returns the other socket descriptor.  You may use this to integrate
 * code into the istream framework which works only with a socket
 * descriptor.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-internal.h"
#include "istream-buffer.h"
#include "fd_util.h"
#include "fd-util.h"
#include "fifo-buffer.h"
#include "buffered_io.h"
#include "pevent.h"

#include <daemon/log.h>

#include <assert.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <limits.h>

struct istream_socketpair {
    struct istream output;
    struct istream *input;

    int fd;

    struct event recv_event, send_event;

    struct fifo_buffer *buffer;
};


static void
socketpair_release_socket(struct istream_socketpair *sp)
{
    assert(sp != NULL);
    assert(sp->fd >= 0);

    p_event_del(&sp->recv_event, sp->output.pool);
    p_event_del(&sp->send_event, sp->output.pool);

    close(sp->fd);
    sp->fd = -1;
}

static void
socketpair_close(struct istream_socketpair *sp, GError *error)
{
    pool_ref(sp->output.pool);

    if (sp->input != NULL)
        istream_free_handler(&sp->input);

    if (sp->fd >= 0) {
        socketpair_release_socket(sp);

        istream_deinit_abort(&sp->output, error);
    } else
        g_error_free(error);

    pool_unref(sp->output.pool);
}


/*
 * istream handler
 *
 */

static size_t
socketpair_input_data(const void *data, size_t length, void *ctx)
{
    struct istream_socketpair *sp = ctx;
    ssize_t nbytes;

    assert(sp->fd >= 0);

    nbytes = send(sp->fd, data, length, MSG_DONTWAIT|MSG_NOSIGNAL);
    if (likely(nbytes >= 0)) {
        p_event_add(&sp->send_event, NULL,
                    sp->output.pool, "socketpair_send_event");
        return (size_t)nbytes;
    }

    if (likely(errno == EAGAIN)) {
        p_event_add(&sp->send_event, NULL,
                    sp->output.pool, "socketpair_send_event");
        return 0;
    }

    GError *error =
        g_error_new(g_file_error_quark(), errno,
                    "write error on socket pair: %s", strerror(errno));
    socketpair_close(sp, error);
    return 0;
}

static void
socketpair_input_eof(void *ctx)
{
    struct istream_socketpair *sp = ctx;

    assert(sp->input != NULL);
    assert(sp->fd >= 0);

    p_event_del(&sp->send_event, sp->output.pool);
    shutdown(sp->fd, SHUT_WR);
    sp->input = NULL;
}

static void
socketpair_input_abort(GError *error, void *ctx)
{
    struct istream_socketpair *sp = ctx;

    assert(sp->input != NULL);

    socketpair_close(sp, error);
}

static const struct istream_handler socketpair_input_handler = {
    .data = socketpair_input_data,
    /* .direct = socketpair_input_direct, XXX */
    .eof = socketpair_input_eof,
    .abort = socketpair_input_abort,
};


/*
 * istream implementation
 *
 */

static inline struct istream_socketpair *
istream_to_socketpair(struct istream *istream)
{
    return (struct istream_socketpair *)(((char*)istream) - offsetof(struct istream_socketpair, output));
}

static void
istream_socketpair_read(struct istream *istream)
{
    struct istream_socketpair *sp = istream_to_socketpair(istream);

    istream_handler_set_direct(sp->input, sp->output.handler_direct);

    istream_read(sp->input);
}

static void
istream_socketpair_close(struct istream *istream)
{
    struct istream_socketpair *sp = istream_to_socketpair(istream);

    if (sp->input != NULL)
        istream_free_handler(&sp->input);

    if (sp->fd >= 0) {
        socketpair_release_socket(sp);

        istream_deinit(&sp->output);
    }
}

static const struct istream_class istream_socketpair = {
    .read = istream_socketpair_read,
    .close = istream_socketpair_close,
};


/*
 * I/O
 *
 */

static void
socketpair_read(struct istream_socketpair *sp)
{
    ssize_t nbytes;

    nbytes = recv_to_buffer(sp->fd, sp->buffer, INT_MAX);
    if (nbytes == -2)
        return;

    if (nbytes < 0) {
        GError *error =
            g_error_new(g_file_error_quark(), errno,
                        "read error on socket pair: %s", strerror(errno));
        socketpair_close(sp, error);
        return;
    }

    if (nbytes == 0) {
        pool_ref(sp->output.pool);

        if (sp->input != NULL)
            istream_free_handler(&sp->input);

        if (sp->fd >= 0) {
            socketpair_release_socket(sp);
            istream_deinit_eof(&sp->output);
        }

        pool_unref(sp->output.pool);

        return;
    }

    if (istream_buffer_send(&sp->output, sp->buffer) > 0)
        p_event_add(&sp->recv_event, NULL,
                    sp->output.pool, "socketpair_recv_event");
}


/*
 * libevent callback
 *
 */

static void
socketpair_recv_callback(int fd gcc_unused, short event gcc_unused,
                         void *ctx)
{
    struct istream_socketpair *sp = ctx;

    assert(fd == sp->fd);

    socketpair_read(sp);

    pool_commit();
}

static void
socketpair_send_callback(int fd gcc_unused, short event gcc_unused,
                         void *ctx)
{
    struct istream_socketpair *sp = ctx;

    assert(fd == sp->fd);

    istream_read(sp->input);

    pool_commit();
}


/*
 * constructor
 *
 */

struct istream *
istream_socketpair_new(struct pool *pool, struct istream *input, int *fd_r)
{
    struct istream_socketpair *sp;
    int ret, fds[2];

    assert(input != NULL);
    assert(!istream_has_handler(input));

    ret = socketpair_cloexec(AF_UNIX, SOCK_STREAM, 0, fds);
    if (ret < 0) {
        daemon_log(1, "socketpair() failed: %s\n", strerror(errno));
        return NULL;
    }

    if (fd_set_nonblock(fds[1], true) < 0) {
        daemon_log(1, "fd_set_nonblock() failed: %s\n", strerror(errno));
        close(fds[0]);
        close(fds[1]);
        return NULL;
    }

    sp = istream_new_macro(pool, socketpair);
    istream_assign_handler(&sp->input, input,
                           &socketpair_input_handler, sp,
                           0);

    sp->fd = fds[0];
    *fd_r = fds[1];

    sp->buffer = fifo_buffer_new(pool, 4096);

    event_set(&sp->recv_event, sp->fd, EV_READ, socketpair_recv_callback, sp);
    p_event_add(&sp->recv_event, NULL,
                sp->output.pool, "socketpair_recv_event");

    event_set(&sp->send_event, sp->fd, EV_WRITE, socketpair_send_callback, sp);
    p_event_add(&sp->send_event, NULL,
                sp->output.pool, "socketpair_send_event");

    return istream_struct_cast(&sp->output);
}
