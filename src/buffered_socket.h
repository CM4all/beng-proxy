/*
 * Wrapper for a socket file descriptor with input buffer management.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_BUFFERED_SOCKET_H
#define BENG_PROXY_BUFFERED_SOCKET_H

#include "socket_wrapper.h"

#include <glib.h>

struct fifo_buffer;

struct buffered_socket_handler {
    /**
     * Data has been read from the socket into the input buffer.  Call
     * buffered_socket_consumed() each time you consume data from the
     * given buffer.
     *
     * @return true if more data shall be read from the socket, false
     * when the socket has been closed or if the output is currently
     * unable to consume data
     */
    bool (*data)(const void *buffer, size_t size, void *ctx);

    /**
     * The socket is ready for reading.  It is suggested to attempt a
     * "direct" tansfer.
     *
     * @return true if more data shall be read from the socket, false
     * when the socket has been closed or if the output is currently
     * unable to consume data
     */
    bool (*direct)(int fd, enum istream_direct fd_type, void *ctx);

    /**
     * The peer has finished sending and has closed the socket.  The
     * method must close/abandon the socket.  There may still be data
     * in the input buffer, so don't give up on this object yet.
     *
     * @param remaining the remaining number of bytes in the input
     * buffer (may be used by the method to see if there's not enough
     * / too much data in the buffer)
     * @return false if no more data shall be delivered to the
     * handler; the #end method will also not be invoked
     */
    bool (*closed)(size_t remaining, void *ctx);

    /**
     * The buffer has become empty after the socket has been closed by
     * the peer.  This may be called right after #closed if the input
     * buffer was empty.
     */
    void (*end)(void *ctx);

    /**
     * The socket is ready for writing.
     *
     * @return false when the socket has been closed
     */
    bool (*write)(void *ctx);

    /**
     * @return false when the socket has been closed
     */
    bool (*timeout)(void *ctx);

    /**
     * An I/O error on the socket has occurred.  After returning, it
     * is assumed that the #buffered_socket object has been closed.
     *
     * @param error a description of the error, to be freed by the
     * callee
     */
    void (*error)(GError *error, void *ctx);
};

/**
 * A wrapper for #socket_wrapper that manages an optional input
 * buffer.
 *
 * The object can have the following states:
 *
 * - uninitialised
 *
 * - connected (after buffered_socket_init())
 *
 * - disconnected (after buffered_socket_close() or
 * buffered_socket_abandon()); in this state, the remaining data
 * from the input buffer will be delivered
 *
 * - ended (when the handler method end() is invoked)
 *
 * - destroyed (after buffered_socket_destroy())
 */
struct buffered_socket {
    struct socket_wrapper base;

    const struct buffered_socket_handler *handler;
    void *handler_ctx;

    struct fifo_buffer *input;

    /**
     * Attempt to do "direct" transfers?
     */
    bool direct;

#ifndef NDEBUG
    bool reading, ended, destroyed;
#endif
};

void
buffered_socket_init(struct buffered_socket *s, struct pool *pool,
                     int fd, enum istream_direct fd_type,
                     const struct timeval *read_timeout,
                     const struct timeval *write_timeout,
                     const struct buffered_socket_handler *handler, void *ctx);

/**
 * Close the physical socket, but do not destroy the input buffer.  To
 * do the latter, call buffered_socket_destroy().
 */
static inline void
buffered_socket_close(struct buffered_socket *s)
{
    assert(!s->ended);
    assert(!s->destroyed);

    socket_wrapper_close(&s->base);
}

/**
 * Just like buffered_socket_close(), but do not actually close the
 * socket.  The caller is responsible for closing the socket (or
 * scheduling it for reuse).
 */
static inline void
buffered_socket_abandon(struct buffered_socket *s)
{
    assert(!s->ended);
    assert(!s->destroyed);

    socket_wrapper_abandon(&s->base);
}

/**
 * Destroy the object.  Prior to that, the socket must be removed by
 * calling either buffered_socket_close() or
 * buffered_socket_abandon().
 */
static inline void
buffered_socket_destroy(struct buffered_socket *s)
{
    assert(!socket_wrapper_valid(&s->base));
    assert(!s->destroyed);

    s->input = NULL;

#ifndef NDEBUG
    s->destroyed = true;
#endif
}

/**
 * Is the socket still connected?  This does not actually check
 * whether the socket is connected, just whether it is known to be
 * closed.
 */
static inline bool
buffered_socket_connected(const struct buffered_socket *s)
{
    assert(s != NULL);
    assert(!s->ended);
    assert(!s->destroyed);

    return socket_wrapper_valid(&s->base);
}

/**
 * Is the object still usable?  The socket may be closed already, but
 * the input buffer may still have data.
 */
static inline bool
buffered_socket_valid(const struct buffered_socket *s)
{
    assert(s != NULL);

    /* the object is valid if there is either a valid socket or a
       buffer that may have more data; in the latter case, the socket
       may be closed already because no more data is needed from
       there */
    return socket_wrapper_valid(&s->base) || s->input != NULL;
}

/**
 * Is the input buffer empty?
 */
gcc_pure
bool
buffered_socket_empty(const struct buffered_socket *s);

/**
 * Is the input buffer full?
 */
gcc_pure
bool
buffered_socket_full(const struct buffered_socket *s);

/**
 * Mark the specified number of bytes of the input buffer as
 * "consumed".  Call this in the data() method.  Note that this method
 * does not invalidate the buffer passed to data().  It may be called
 * repeatedly.
 */
void
buffered_socket_consumed(struct buffered_socket *s, size_t nbytes);

/**
 * Returns the istream_direct mask for splicing data into this socket.
 */
static inline enum istream_direct
buffered_socket_direct_mask(const struct buffered_socket *s)
{
    assert(s != NULL);
    assert(!s->ended);
    assert(!s->destroyed);

    return socket_wrapper_direct_mask(&s->base);
}

/**
 * The caller wants to read more data from the socket.  There are four
 * possible outcomes: a call to buffered_socket_handler.read, a call
 * to buffered_socket_handler.direct, a call to
 * buffered_socket_handler.error or (if there is no data available
 * yet) an event gets scheduled and the function returns immediately.
 */
bool
buffered_socket_read(struct buffered_socket *s);

static inline ssize_t
buffered_socket_write(struct buffered_socket *s,
                      const void *data, size_t length)
{
    return socket_wrapper_write(&s->base, data, length);
}

static inline ssize_t
buffered_socket_write_from(struct buffered_socket *s,
                           int fd, enum istream_direct fd_type,
                           size_t length)
{
    return socket_wrapper_write_from(&s->base, fd, fd_type, length);
}

gcc_pure
static inline bool
buffered_socket_ready_for_writing(const struct buffered_socket *s)
{
    assert(!s->destroyed);

    return socket_wrapper_ready_for_writing(&s->base);
}

static inline void
buffered_socket_schedule_write(struct buffered_socket *s)
{
    assert(!s->ended);
    assert(!s->destroyed);

    socket_wrapper_schedule_write(&s->base);
}

static inline void
buffered_socket_unschedule_write(struct buffered_socket *s)
{
    assert(!s->ended);
    assert(!s->destroyed);

    socket_wrapper_unschedule_write(&s->base);
}

#endif
