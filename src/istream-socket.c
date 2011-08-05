/*
 * An istream receiving data from a socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-socket.h"
#include "istream-internal.h"
#include "istream-buffer.h"
#include "fifo-buffer.h"
#include "buffered-io.h"
#include "pevent.h"

#include <event.h>
#include <errno.h>

struct istream_socket {
    struct istream output;

    int fd;
    istream_direct_t fd_type;
    const struct istream_socket_handler *handler;
    void *handler_ctx;

    struct fifo_buffer *buffer;

    struct event event;
};

static void
socket_schedule_read(struct istream_socket *s)
{
    assert(s->fd >= 0);
    assert(s->buffer == NULL || !fifo_buffer_full(s->buffer));

    p_event_add(&s->event, NULL, s->output.pool, "istream_socket");
}

static void
socket_try_direct(struct istream_socket *s)
{
    if (s->buffer != NULL && istream_buffer_consume(&s->output, s->buffer) > 0)
        return;

    ssize_t nbytes = istream_invoke_direct(&s->output, s->fd_type, s->fd,
                                           G_MAXINT);
    if (G_LIKELY(nbytes > 0)) {
        /* schedule the next read */
        socket_schedule_read(s);
    } else if (nbytes == 0) {
        if (s->handler->depleted(s->handler_ctx) &&
            s->handler->finished(s->handler_ctx))
            istream_deinit_eof(&s->output);
    } else if (nbytes == -2 || nbytes == -3) {
        /* either the destination fd blocks (-2) or the stream (and
           the whole connection) has been closed during the direct()
           callback (-3); no further checks */
        return;
    } else if (errno == EAGAIN) {
        /* wait for the socket */
        socket_schedule_read(s);
    } else {
        const int e = errno;
        s->handler->error(e, s->handler_ctx);

        GError *error = g_error_new(g_file_error_quark(), e,
                                    "recv error: %s", strerror(e));
        istream_deinit_abort(&s->output, error);
    }
}

static void
socket_try_buffered(struct istream_socket *s)
{
    if (s->buffer == NULL)
        s->buffer = fifo_buffer_new(s->output.pool, 8192);

    assert(!fifo_buffer_full(s->buffer));

    if (s->buffer != NULL && istream_buffer_consume(&s->output, s->buffer) > 0)
        return;

    ssize_t nbytes = recv_to_buffer(s->fd, s->buffer, G_MAXINT);
    if (G_LIKELY(nbytes > 0)) {
        size_t consumed = istream_buffer_send(&s->output, s->buffer);
        if (consumed > 0)
            socket_schedule_read(s);
    } else if (nbytes == 0) {
        if (s->handler->depleted(s->handler_ctx) &&
            s->handler->finished(s->handler_ctx))
            istream_deinit_eof(&s->output);
    } else if (errno == EAGAIN) {
        socket_schedule_read(s);
    } else {
        const int e = errno;
        s->handler->error(e, s->handler_ctx);

        GError *error = g_error_new(g_file_error_quark(), e,
                                    "recv error: %s", strerror(e));
        istream_deinit_abort(&s->output, error);
    }
}

static void
socket_try_read(struct istream_socket *s)
{
    if (istream_check_direct(&s->output, s->fd_type))
        socket_try_direct(s);
    else
        socket_try_buffered(s);
}

/*
 * istream implementation
 *
 */

static inline struct istream_socket *
istream_to_socket(struct istream *istream)
{
    return (struct istream_socket *)(((char*)istream) - offsetof(struct istream_socket, output));
}

static off_t
istream_socket_available(istream_t istream, bool partial)
{
    struct istream_socket *s = istream_to_socket(istream);

    if (s->buffer == NULL || (!partial && s->fd >= 0))
        return -1;

    return fifo_buffer_available(s->buffer);
}

static void
istream_socket_read(struct istream *istream)
{
    struct istream_socket *s = istream_to_socket(istream);

    socket_try_read(s);
}

static void
istream_socket_close(struct istream *istream)
{
    struct istream_socket *s = istream_to_socket(istream);

    p_event_del(&s->event, s->output.pool);

    s->handler->close(s->handler_ctx);

    istream_deinit(&s->output);
}

static const struct istream istream_socket = {
    .available = istream_socket_available,
    .read = istream_socket_read,
    .close = istream_socket_close,
};

/*
 * libevent callback
 *
 */

static void
socket_event_callback(int fd __attr_unused, short event __attr_unused,
                      void *ctx)
{
    struct istream_socket *s = ctx;

    assert(fd == s->fd);

    socket_try_read(s);

    pool_commit();
}

/*
 * constructor
 *
 */

struct istream *
istream_socket_new(struct pool *pool, int fd, istream_direct_t fd_type,
                   const struct istream_socket_handler *handler, void *ctx)
{
    assert(fd >= 0);
    assert(handler != NULL);
    assert(handler->close != NULL);
    assert(handler->error != NULL);
    assert(handler->depleted != NULL);
    assert(handler->finished != NULL);

    struct istream_socket *s = istream_new_macro(pool, socket);
    s->fd = fd;
    s->fd_type = fd_type;
    s->handler = handler;
    s->handler_ctx = ctx;

    s->buffer = NULL;

    event_set(&s->event, fd, EV_READ, socket_event_callback, s);
    socket_schedule_read(s);

    return &s->output;
}
