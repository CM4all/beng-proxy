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
#include "event2.h"
#include "socket-util.h"
#include "fifo-buffer.h"
#include "buffered-io.h"

#include <daemon/log.h>

#include <assert.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

struct istream_socketpair {
    struct istream output;
    istream_t input;

    int fd;
    struct event2 event;

    struct fifo_buffer *buffer;
};


static void
socketpair_close(struct istream_socketpair *sp)
{
    pool_ref(sp->output.pool);

    if (sp->input != NULL)
        istream_close(sp->input);

    if (sp->fd >= 0) {
        event2_set(&sp->event, 0);
        event2_commit(&sp->event);

        close(sp->fd);
        sp->fd = -1;

        istream_deinit_abort(&sp->output);
    }

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

    nbytes = write(sp->fd, data, length);
    if (likely(nbytes >= 0)) {
        event2_or(&sp->event, EV_WRITE);
        return (size_t)nbytes;
    }

    if (likely(errno == EAGAIN)) {
        event2_or(&sp->event, EV_WRITE);
        return 0;
    }

    daemon_log(1, "write error on socket pair: %s\n", strerror(errno));
    socketpair_close(sp);
    return 0;
}

static void
socketpair_input_eof(void *ctx)
{
    struct istream_socketpair *sp = ctx;

    assert(sp->input != NULL);
    assert(sp->fd >= 0);

    event2_nand(&sp->event, EV_WRITE);
    shutdown(sp->fd, SHUT_WR);
    sp->input = NULL;
}

static void
socketpair_input_abort(void *ctx)
{
    struct istream_socketpair *sp = ctx;

    assert(sp->input != NULL);

    socketpair_close(sp);
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
istream_to_socketpair(istream_t istream)
{
    return (struct istream_socketpair *)(((char*)istream) - offsetof(struct istream_socketpair, output));
}

static void
istream_socketpair_read(istream_t istream)
{
    struct istream_socketpair *sp = istream_to_socketpair(istream);

    istream_handler_set_direct(sp->input, sp->output.handler_direct);

    istream_read(sp->input);
}

static void
istream_socketpair_close(istream_t istream)
{
    struct istream_socketpair *sp = istream_to_socketpair(istream);

    socketpair_close(sp);
}

static const struct istream istream_socketpair = {
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
    size_t rest;

    nbytes = read_to_buffer(sp->fd, sp->buffer, INT_MAX);
    if (nbytes == -2) {
        event2_nand(&sp->event, EV_WRITE);
        return;
    }

    if (nbytes < 0) {
        daemon_log(1, "read from socketpair failed: %s\n", strerror(errno));
        socketpair_close(sp);
        return;
    }

    rest = istream_buffer_consume(&sp->output, sp->buffer);
    if (rest > 0)
        event2_nand(&sp->event, EV_READ);
}


/*
 * libevent callback
 *
 */

static void
socketpair_event_callback(int fd __attr_unused, short event, void *ctx)
{
    struct istream_socketpair *sp = ctx;

    assert(fd == sp->fd);

    pool_ref(sp->output.pool);

    if ((event & EV_READ) != 0)
        socketpair_read(sp);

    if (sp->input != NULL && (event & EV_WRITE) != 0) {
        istream_read(sp->input);
    }

    pool_unref(sp->output.pool);
    pool_commit();
}


/*
 * constructor
 *
 */

istream_t
istream_socketpair_new(pool_t pool, istream_t input, int *fd_r)
{
    struct istream_socketpair *sp;
    int ret, fds[2];

    assert(input != NULL);
    assert(!istream_has_handler(input));

    ret = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    if (ret < 0) {
        daemon_log(1, "socketpair() failed: %s\n", strerror(errno));
        return NULL;
    }

    if (socket_set_nonblock(fds[0], true) < 0 ||
        socket_set_nonblock(fds[1], true) < 0) {
        daemon_log(1, "socket_set_nonblock() failed: %s\n", strerror(errno));
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

    event2_init(&sp->event, sp->fd,
                socketpair_event_callback, sp,
                NULL);
    event2_set(&sp->event, EV_READ|EV_WRITE);

    sp->buffer = fifo_buffer_new(pool, 4096);

    return istream_struct_cast(&sp->output);
}
