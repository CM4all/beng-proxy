/*
 * An istream receiving data from a socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-socket.h"
#include "istream-internal.h"
#include "istream-buffer.h"
#include "fifo-buffer.h"
#include "buffered_io.h"
#include "pevent.h"
#include "gerrno.h"
#include "fb_pool.h"

#include <event.h>
#include <errno.h>
#include <string.h>

struct istream_socket {
    struct istream output;

    /**
     * The socket descriptor.  Will be set to -1 when the stream is
     * closed.
     */
    int fd;

    istream_direct_t fd_type;
    const struct istream_socket_handler *handler;
    void *handler_ctx;

    struct fifo_buffer *buffer;

    struct event event;
};

static inline bool
socket_valid(const struct istream_socket *s)
{
    assert(s != NULL);

    return s->fd >= 0;
}

static void
socket_schedule_read(struct istream_socket *s)
{
    assert(socket_valid(s));
    assert(s->buffer == NULL || !fifo_buffer_full(s->buffer));

    p_event_add(&s->event, NULL, s->output.pool, "istream_socket");
}

/**
 * @return true if there is still data in the buffer (or if the stream
 * has been closed), false if the buffer is empty
 */
static bool
socket_buffer_consume(struct istream_socket *s)
{
    assert(socket_valid(s));
    assert(s->buffer != NULL);

    if (gcc_likely(!fifo_buffer_full(s->buffer) ||
                   s->handler->full == NULL))
        /* quick path without an additional pool reference */
        return istream_buffer_consume(&s->output, s->buffer) > 0;

    pool_ref(s->output.pool);
    const bool full = istream_buffer_send(&s->output, s->buffer) == 0 &&
        socket_valid(s);
    const bool empty = !full && socket_valid(s) &&
        fifo_buffer_empty(s->buffer);
    pool_unref(s->output.pool);

    if (gcc_unlikely(full && !s->handler->full(s->handler_ctx)))
        /* stream has been closed */
        return true;

    return !empty;
}

/**
 * @return true if data was consumed, false if the istream handler is
 * blocking (or if the stream has been closed)
 */
static bool
socket_buffer_send(struct istream_socket *s)
{
    assert(socket_valid(s));
    assert(s->buffer != NULL);

    if (gcc_likely(!fifo_buffer_full(s->buffer) ||
                   s->handler->full == NULL))
        /* quick path without an additional pool reference */
        return istream_buffer_send(&s->output, s->buffer) > 0;

    pool_ref(s->output.pool);
    const bool consumed = istream_buffer_send(&s->output, s->buffer) > 0;
    const bool full = !consumed && socket_valid(s);
    pool_unref(s->output.pool);

    if (full)
        s->handler->full(s->handler_ctx);

    return consumed;
}

static void
socket_try_direct(struct istream_socket *s)
{
    assert(socket_valid(s));

    if (s->buffer != NULL) {
        if (socket_buffer_consume(s))
            return;

        fb_pool_free(s->buffer);
        s->buffer = NULL;
    }

    ssize_t nbytes = istream_invoke_direct(&s->output, s->fd_type, s->fd,
                                           G_MAXINT);
    if (G_LIKELY(nbytes > 0)) {
        /* schedule the next read */
        socket_schedule_read(s);
    } else if (nbytes == 0) {
        if (s->handler->depleted(s->handler_ctx) &&
            s->handler->finished(s->handler_ctx)) {
            s->fd = -1;
            istream_deinit_eof(&s->output);
        }
    } else if (nbytes == ISTREAM_RESULT_BLOCKING ||
               nbytes == ISTREAM_RESULT_CLOSED) {
        /* either the destination fd blocks (-2) or the stream (and
           the whole connection) has been closed during the direct()
           callback (-3); no further checks */
        return;
    } else if (errno == EAGAIN) {
        /* wait for the socket */
        socket_schedule_read(s);
    } else {
        const int e = errno;
        if (!s->handler->error(e, s->handler_ctx))
            return;

        GError *error = new_error_errno_msg2(e, "recv error");
        s->fd = -1;
        istream_deinit_abort(&s->output, error);
    }
}

static void
socket_try_buffered(struct istream_socket *s)
{
    assert(socket_valid(s));

    if (s->buffer == NULL)
        s->buffer = fb_pool_alloc();
    else if (socket_buffer_consume(s))
        return;

    assert(!fifo_buffer_full(s->buffer));

    ssize_t nbytes = recv_to_buffer(s->fd, s->buffer, G_MAXINT);
    if (G_LIKELY(nbytes > 0)) {
        if (socket_buffer_send(s))
            socket_schedule_read(s);
    } else if (nbytes == 0) {
        if (s->handler->depleted(s->handler_ctx) &&
            s->handler->finished(s->handler_ctx)) {
            fb_pool_free(s->buffer);
            s->fd = -1;
            istream_deinit_eof(&s->output);
        }
    } else if (errno == EAGAIN) {
        socket_schedule_read(s);
    } else {
        const int e = errno;

        fb_pool_free(s->buffer);
        s->buffer = NULL;

        if (!s->handler->error(e, s->handler_ctx))
            return;

        GError *error = new_error_errno_msg2(e, "recv error");
        s->fd = -1;
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
istream_socket_available(struct istream *istream, bool partial)
{
    struct istream_socket *s = istream_to_socket(istream);

    assert(socket_valid(s));

    if (s->buffer == NULL || (!partial && s->fd >= 0))
        return -1;

    return fifo_buffer_available(s->buffer);
}

static void
istream_socket_read(struct istream *istream)
{
    struct istream_socket *s = istream_to_socket(istream);

    assert(socket_valid(s));

    socket_try_read(s);
}

static void
istream_socket_close(struct istream *istream)
{
    struct istream_socket *s = istream_to_socket(istream);

    assert(socket_valid(s));

    if (s->buffer != NULL) {
        fb_pool_free(s->buffer);
        s->buffer = NULL;
    }

    p_event_del(&s->event, s->output.pool);
    s->fd = -1;

    s->handler->close(s->handler_ctx);

    istream_deinit(&s->output);
}

static const struct istream_class istream_socket = {
    .available = istream_socket_available,
    .read = istream_socket_read,
    .close = istream_socket_close,
};

/*
 * libevent callback
 *
 */

static void
socket_event_callback(int fd gcc_unused, short event gcc_unused,
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
