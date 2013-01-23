/*
 * Wrapper for a socket descriptor with (optional) filter for input
 * and output.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_FILTERED_SOCKET_H
#define BENG_PROXY_FILTERED_SOCKET_H

#include "buffered_socket.h"

#include <pthread.h>

struct fifo_buffer;

struct socket_filter {
    /**
     * Data has been read from the socket into the input buffer.  Call
     * filtered_socket_internal_consumed() each time you consume data
     * from the given buffer.
     */
    enum buffered_result (*data)(const void *buffer, size_t size, void *ctx);

    bool (*is_empty)(void *ctx);

    bool (*is_full)(void *ctx);

    size_t (*available)(void *ctx);

    void (*consumed)(size_t nbytes, void *ctx);

    /**
     * The client asks to read more data.  The filter shall call
     * filtered_socket_internal_data() again.
     */
    bool (*read)(void *ctx);

    /**
     * The client asks to write data to the socket.  The filter
     * processes it, and may then call
     * filtered_socket_internal_write().
     */
    ssize_t (*write)(const void *data, size_t length, void *ctx);

    /**
     * The underlying socket is ready for writing.  The filter may try
     * calling filtered_socket_internal_write() again.
     *
     * This method must not destroy the socket.  If an error occurs,
     * it shall return false with a GError.
     */
    bool (*internal_write)(void *ctx, GError **error_r);

    bool (*closed)(size_t remaining, void *ctx);

    void (*close)(void *ctx);
};

/**
 * A wrapper for #buffered_socket that can filter input and output.
 */
struct filtered_socket {
    struct buffered_socket base;

    /**
     * The actual filter.  If this is NULL, then this object behaves
     * just like #buffered_socket.
     */
    const struct socket_filter *filter;
    void *filter_ctx;

    const struct buffered_socket_handler *handler;
    void *handler_ctx;
};

gcc_const
static inline GQuark
filtered_socket_quark(void)
{
    return g_quark_from_static_string("filtered_socket");
}

/**
 * Initialise the object with a filter.
 */
void
filtered_socket_init(struct filtered_socket *s, struct pool *pool,
                     int fd, enum istream_direct fd_type,
                     const struct timeval *read_timeout,
                     const struct timeval *write_timeout,
                     const struct socket_filter *filter,
                     void *filter_ctx,
                     const struct buffered_socket_handler *handler,
                     void *handler_ctx);

/**
 * Initialise the object without a filter.
 */
static inline void
filtered_socket_init_null(struct filtered_socket *s, struct pool *pool,
                          int fd, enum istream_direct fd_type,
                          const struct timeval *read_timeout,
                          const struct timeval *write_timeout,
                          const struct buffered_socket_handler *handler,
                          void *handler_ctx)
{
    buffered_socket_init(&s->base, pool, fd, fd_type,
                         read_timeout, write_timeout,
                         handler, handler_ctx);

    s->filter = NULL;
}

static inline enum istream_direct
filtered_socket_fd_type(const struct filtered_socket *s)
{
    return s->filter == NULL
        ? s->base.base.fd_type
        /* can't do splice() with a filter */
        : ISTREAM_NONE;
}

/**
 * Close the physical socket, but do not destroy the input buffer.  To
 * do the latter, call filtered_socket_destroy().
 */
static inline void
filtered_socket_close(struct filtered_socket *s)
{
    buffered_socket_close(&s->base);
}

/**
 * Just like filtered_socket_close(), but do not actually close the
 * socket.  The caller is responsible for closing the socket (or
 * scheduling it for reuse).
 */
static inline void
filtered_socket_abandon(struct filtered_socket *s)
{
    buffered_socket_abandon(&s->base);
}

/**
 * Destroy the object.  Prior to that, the socket must be removed by
 * calling either filtered_socket_close() or
 * filtered_socket_abandon().
 */
void
filtered_socket_destroy(struct filtered_socket *s);

/**
 * Returns the socket descriptor and calls filtered_socket_abandon().
 * Returns -1 if the input buffer is not empty.
 */
static inline int
filtered_socket_as_fd(struct filtered_socket *s)
{
    return s->filter != NULL
        ? -1
        : buffered_socket_as_fd(&s->base);
}

/**
 * Is the socket still connected?  This does not actually check
 * whether the socket is connected, just whether it is known to be
 * closed.
 */
static inline bool
filtered_socket_connected(const struct filtered_socket *s)
{
    return buffered_socket_connected(&s->base);
}

/**
 * Is the object still usable?  The socket may be closed already, but
 * the input buffer may still have data.
 */
static inline bool
filtered_socket_valid(const struct filtered_socket *s)
{
    assert(s != NULL);

    return buffered_socket_valid(&s->base);
}

/**
 * Is the input buffer empty?
 */
gcc_pure
bool
filtered_socket_empty(const struct filtered_socket *s);

/**
 * Is the input buffer full?
 */
gcc_pure
bool
filtered_socket_full(const struct filtered_socket *s);

/**
 * Returns the number of bytes in the input buffer.
 */
gcc_pure
size_t
filtered_socket_available(const struct filtered_socket *s);

/**
 * Mark the specified number of bytes of the input buffer as
 * "consumed".  Call this in the data() method.  Note that this method
 * does not invalidate the buffer passed to data().  It may be called
 * repeatedly.
 */
void
filtered_socket_consumed(struct filtered_socket *s, size_t nbytes);

/**
 * Returns the istream_direct mask for splicing data into this socket.
 */
static inline enum istream_direct
filtered_socket_direct_mask(const struct filtered_socket *s)
{
    assert(s != NULL);

    return s->filter != NULL
        ? ISTREAM_NONE
        : buffered_socket_direct_mask(&s->base);
}

/**
 * The caller wants to read more data from the socket.  There are four
 * possible outcomes: a call to filtered_socket_handler.read, a call
 * to filtered_socket_handler.direct, a call to
 * filtered_socket_handler.error or (if there is no data available
 * yet) an event gets scheduled and the function returns immediately.
 */
bool
filtered_socket_read(struct filtered_socket *s);

static inline void
filtered_socket_set_cork(struct filtered_socket *s, bool cork)
{
    buffered_socket_set_cork(&s->base, cork);
}

ssize_t
filtered_socket_write(struct filtered_socket *s,
                      const void *data, size_t length);

static inline ssize_t
filtered_socket_write_from(struct filtered_socket *s,
                           int fd, enum istream_direct fd_type,
                           size_t length)
{
    assert(s->filter == NULL);

    return buffered_socket_write_from(&s->base, fd, fd_type, length);
}

gcc_pure
static inline bool
filtered_socket_ready_for_writing(const struct filtered_socket *s)
{
    assert(s->filter == NULL);

    return buffered_socket_ready_for_writing(&s->base);
}

static inline void
filtered_socket_schedule_read_timeout(struct filtered_socket *s,
                                      const struct timeval *timeout)
{
    buffered_socket_schedule_read_timeout(&s->base, timeout);
}

/**
 * Schedules reading on the socket with timeout disabled, to indicate
 * that you are willing to read, but do not expect it yet.  No direct
 * action is taken.  Use this to enable reading when you are still
 * sending the request.  When you are finished sending the request,
 * you should call filtered_socket_read() to enable the read timeout.
 */
static inline void
filtered_socket_schedule_read_no_timeout(struct filtered_socket *s)
{
    filtered_socket_schedule_read_timeout(s, NULL);
}

static inline void
filtered_socket_schedule_write(struct filtered_socket *s)
{
    buffered_socket_schedule_write(&s->base);
}

static inline void
filtered_socket_unschedule_write(struct filtered_socket *s)
{
    buffered_socket_unschedule_write(&s->base);
}

static inline enum buffered_result
filtered_socket_internal_data(struct filtered_socket *s,
                              const void *data, size_t size)
{
    assert(s->filter != NULL);

    return s->handler->data(data, size, s->handler_ctx);
}

static inline void
filtered_socket_internal_consumed(struct filtered_socket *s, size_t nbytes)
{
    assert(s->filter != NULL);

    buffered_socket_consumed(&s->base, nbytes);
}

static inline bool
filtered_socket_internal_read(struct filtered_socket *s)
{
    assert(s->filter != NULL);

    return buffered_socket_read(&s->base);
}

static inline ssize_t
filtered_socket_internal_write(struct filtered_socket *s,
                               const void *data, size_t length)
{
    assert(s->filter != NULL);

    return buffered_socket_write(&s->base, data, length);
}

#endif
