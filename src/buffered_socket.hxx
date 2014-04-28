/*
 * Wrapper for a socket file descriptor with input buffer management.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_BUFFERED_SOCKET_HXX
#define BENG_PROXY_BUFFERED_SOCKET_HXX

#include "socket_wrapper.h"

#include <glib.h>

struct fifo_buffer;

enum buffered_result {
    /**
     * The handler has consumed all data successfully, and is willing
     * to receive more data.
     */
    BUFFERED_OK,

    /**
     * The handler has consumed some data successfully, and is willing
     * to receive more data.
     */
    BUFFERED_PARTIAL,

    /**
     * The handler needs more data to finish the operation.  If no
     * more data can be obtained (because the socket has been closed
     * already), the caller is responsible for generating an error.
     * If the input buffer is already full, an error will be
     * generated, too.
     */
    BUFFERED_MORE,

    /**
     * The handler wants to be called again immediately, without
     * attempting to read more data from the socket.  This result code
     * can be used to simplify the handler code.
     *
     * If the input buffer is empty, this return value behaves like
     * #BUFFERED_OK or #BUFFERED_PARTIAL.
     */
    BUFFERED_AGAIN_OPTIONAL,

    /**
     * The handler wants to be called again immediately, without
     * attempting to read more data from the socket.  This result code
     * can be used to simplify the handler code.
     *
     * If the input buffer is empty, this return value behaves like
     * #BUFFERED_MORE.
     */
    BUFFERED_AGAIN_EXPECT,

    /**
     * The handler blocks.  The handler is responsible for calling
     * buffered_socket_read() as soon as it's ready for more data.
     */
    BUFFERED_BLOCKING,

    /**
     * The buffered_socket object has been closed by the handler.
     */
    BUFFERED_CLOSED,
};

enum direct_result {
    /**
     * Some data has been read from the provided socket.
     */
    DIRECT_OK,

    /**
     * The handler blocks.  The handler is responsible for calling
     * buffered_socket_read() as soon as it's ready for more data.
     */
    DIRECT_BLOCKING,

    /**
     * The provided socket blocks.  The caller is responsible for
     * listening on the socket.
     */
    DIRECT_EMPTY,

    /**
     * The handler has determined that no more data can be received on
     * the provided socket, because the peer has closed it.
     */
    DIRECT_END,

    /**
     * The buffered_socket object has been closed by the handler.
     */
    DIRECT_CLOSED,

    /**
     * There was an I/O error on the socket and errno contains the
     * error code.  The caller will create a GError object and will
     * invoke the error() handler method.
     */
    DIRECT_ERRNO,
};

/**
 * Special return values for buffered_socket_write() and
 * buffered_socket_write_from().
 */
enum write_result {
    /**
     * The source has reached end-of-file.  Only valid for
     * buffered_socket_write_from(), i.e. when there is a source file
     * descriptor.
     */
    WRITE_SOURCE_EOF = 0,

    /**
     * An I/O error has occurred, and errno is set.
     */
    WRITE_ERRNO = -1,

    /**
     * The destination socket blocks.  The #buffered_socket library
     * has already scheduled the "write" event to resume writing
     * later.
     */
    WRITE_BLOCKING = -2,

    /**
     * The #buffered_socket was destroyed inside the function call.
     */
    WRITE_DESTROYED = -3,

    /**
     * See buffered_socket_handler::broken
     */
    WRITE_BROKEN = -4,
};

struct buffered_socket_handler {
    /**
     * Data has been read from the socket into the input buffer.  Call
     * buffered_socket_consumed() each time you consume data from the
     * given buffer.
     */
    enum buffered_result (*data)(const void *buffer, size_t size, void *ctx);

    /**
     * The socket is ready for reading.  It is suggested to attempt a
     * "direct" tansfer.
     */
    enum direct_result (*direct)(int fd, enum istream_direct fd_type,
                                 void *ctx);

    /**
     * The peer has finished sending and has closed the socket.  The
     * method must close/abandon the socket.  There may still be data
     * in the input buffer, so don't give up on this object yet.
     *
     * At this time, it may be unknown how much data remains in the
     * input buffer.  As soon as that becomes known, the method
     * #remaining is called (even if it's 0 bytes).
     *
     * @return false if no more data shall be delivered to the
     * handler; the methods #remaining and #end will also not be
     * invoked
     */
    bool (*closed)(void *ctx);

    /**
     * This method gets called after #closed, as soon as the remaining
     * amount of data is known (even if it's 0 bytes).
     *
     * @param remaining the remaining number of bytes in the input
     * buffer (may be used by the method to see if there's not enough
     * / too much data in the buffer)
     * @return false if no more data shall be delivered to the
     * handler; the #end method will also not be invoked
     */
    bool (*remaining)(size_t remaining, void *ctx);

    /**
     * The buffer has become empty after the socket has been closed by
     * the peer.  This may be called right after #closed if the input
     * buffer was empty.
     *
     * If this method is not implemented, a "closed prematurely" error
     * is thrown.
     */
    void (*end)(void *ctx);

    /**
     * The socket is ready for writing.
     *
     * @return false when the socket has been closed
     */
    bool (*write)(void *ctx);

    /**
     * The output buffer was drained, and all data that has been
     * passed to buffered_socket_write() was written to the socket.
     *
     * This method is not actually used by #buffered_socket; it is
     * only implemented for #filtered_socket.
     *
     * @return false if the method has destroyed the socket
     */
    bool (*drained)(void *ctx);

    /**
     * @return false when the socket has been closed
     */
    bool (*timeout)(void *ctx);

    /**
     * A write failed because the peer has closed (at least one side
     * of) the socket.  It may be possible for us to continue reading
     * from the socket.
     *
     * @return true to continue reading from the socket (by returning
     * #WRITE_BROKEN), false if the caller shall close the socket with
     * the error
     */
    bool (*broken)(void *ctx);

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

    const struct timeval *read_timeout, *write_timeout;

    const struct buffered_socket_handler *handler;
    void *handler_ctx;

    struct fifo_buffer *input;

    /**
     * Attempt to do "direct" transfers?
     */
    bool direct;

    /**
     * Does the handler expect more data?  It announced this by
     * returning BUFFERED_MORE.
     */
    bool expect_more;

    /**
     * Set to true each time data was received from the socket.
     */
    bool got_data;

#ifndef NDEBUG
    bool reading, ended, destroyed;

    enum buffered_result last_buffered_result;
#endif
};

gcc_const
static inline GQuark
buffered_socket_quark(void)
{
    return g_quark_from_static_string("buffered_socket");
}

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
void
buffered_socket_destroy(struct buffered_socket *s);

/**
 * Returns the socket descriptor and calls buffered_socket_abandon().
 * Returns -1 if the input buffer is not empty.
 */
int
buffered_socket_as_fd(struct buffered_socket *s);

/**
 * Is the socket still connected?  This does not actually check
 * whether the socket is connected, just whether it is known to be
 * closed.
 */
static inline bool
buffered_socket_connected(const struct buffered_socket *s)
{
    assert(s != nullptr);
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
    assert(s != nullptr);

    /* the object is valid if there is either a valid socket or a
       buffer that may have more data; in the latter case, the socket
       may be closed already because no more data is needed from
       there */
    return socket_wrapper_valid(&s->base) || s->input != nullptr;
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
 * Returns the number of bytes in the input buffer.
 */
gcc_pure
size_t
buffered_socket_available(const struct buffered_socket *s);

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
    assert(s != nullptr);
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
 *
 * @param expect_more if true, generates an error if no more data can
 * be read (socket already shut down, buffer empty); if false, the
 * existing expect_more state is unmodified
 */
bool
buffered_socket_read(struct buffered_socket *s, bool expect_more);

static inline void
buffered_socket_set_cork(struct buffered_socket *s, bool cork)
{
    socket_wrapper_set_cork(&s->base, cork);
}

/**
 * Write data to the socket.
 *
 * @return the positive number of bytes written or a #write_result
 * code
 */
ssize_t
buffered_socket_write(struct buffered_socket *s,
                      const void *data, size_t length);

/**
 * Transfer data from the given file descriptor to the socket.
 *
 * @return the positive number of bytes transferred or a #write_result
 * code
 */
ssize_t
buffered_socket_write_from(struct buffered_socket *s,
                           int fd, enum istream_direct fd_type,
                           size_t length);

gcc_pure
static inline bool
buffered_socket_ready_for_writing(const struct buffered_socket *s)
{
    assert(!s->destroyed);

    return socket_wrapper_ready_for_writing(&s->base);
}

static inline void
buffered_socket_schedule_read_timeout(struct buffered_socket *s,
                                      bool expect_more,
                                      const struct timeval *timeout)
{
    assert(!s->ended);
    assert(!s->destroyed);

    if (expect_more)
        s->expect_more = true;

    s->read_timeout = timeout;
    socket_wrapper_schedule_read(&s->base, timeout);
}

/**
 * Schedules reading on the socket with timeout disabled, to indicate
 * that you are willing to read, but do not expect it yet.  No direct
 * action is taken.  Use this to enable reading when you are still
 * sending the request.  When you are finished sending the request,
 * you should call buffered_socket_read() to enable the read timeout.
 */
static inline void
buffered_socket_schedule_read_no_timeout(struct buffered_socket *s,
                                         bool expect_more)
{
    buffered_socket_schedule_read_timeout(s, expect_more, nullptr);
}

static inline void
buffered_socket_schedule_write(struct buffered_socket *s)
{
    assert(!s->ended);
    assert(!s->destroyed);

    socket_wrapper_schedule_write(&s->base, s->write_timeout);
}

static inline void
buffered_socket_unschedule_write(struct buffered_socket *s)
{
    assert(!s->ended);
    assert(!s->destroyed);

    socket_wrapper_unschedule_write(&s->base);
}

#endif
