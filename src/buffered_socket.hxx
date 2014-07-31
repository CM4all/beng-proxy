/*
 * Wrapper for a socket file descriptor with input buffer management.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_BUFFERED_SOCKET_HXX
#define BENG_PROXY_BUFFERED_SOCKET_HXX

#include "socket_wrapper.hxx"
#include "defer_event.h"

#include <glib.h>

struct fifo_buffer;

enum class BufferedResult {
    /**
     * The handler has consumed all data successfully, and is willing
     * to receive more data.
     */
    OK,

    /**
     * The handler has consumed some data successfully, and is willing
     * to receive more data.
     */
    PARTIAL,

    /**
     * The handler needs more data to finish the operation.  If no
     * more data can be obtained (because the socket has been closed
     * already), the caller is responsible for generating an error.
     * If the input buffer is already full, an error will be
     * generated, too.
     */
    MORE,

    /**
     * The handler wants to be called again immediately, without
     * attempting to read more data from the socket.  This result code
     * can be used to simplify the handler code.
     *
     * If the input buffer is empty, this return value behaves like
     * #OK or #PARTIAL.
     */
    AGAIN_OPTIONAL,

    /**
     * The handler wants to be called again immediately, without
     * attempting to read more data from the socket.  This result code
     * can be used to simplify the handler code.
     *
     * If the input buffer is empty, this return value behaves like
     * #MORE.
     */
    AGAIN_EXPECT,

    /**
     * The handler blocks.  The handler is responsible for calling
     * BufferedSocket::Read() as soon as it's ready for more data.
     */
    BLOCKING,

    /**
     * The buffered_socket object has been closed by the handler.
     */
    CLOSED,
};

enum class DirectResult {
    /**
     * Some data has been read from the provided socket.
     */
    OK,

    /**
     * The handler blocks.  The handler is responsible for calling
     * BufferedSocket::Read() as soon as it's ready for more data.
     */
    BLOCKING,

    /**
     * The provided socket blocks.  The caller is responsible for
     * listening on the socket.
     */
    EMPTY,

    /**
     * The handler has determined that no more data can be received on
     * the provided socket, because the peer has closed it.
     */
    END,

    /**
     * The buffered_socket object has been closed by the handler.
     */
    CLOSED,

    /**
     * There was an I/O error on the socket and errno contains the
     * error code.  The caller will create a GError object and will
     * invoke the error() handler method.
     */
    ERRNO,
};

/**
 * Special return values for BufferedSocket::Write() and
 * BufferedSocket::WriteFrom().
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

struct BufferedSocketHandler {
    /**
     * Data has been read from the socket into the input buffer.  Call
     * buffered_socket_consumed() each time you consume data from the
     * given buffer.
     */
    BufferedResult (*data)(const void *buffer, size_t size, void *ctx);

    /**
     * The socket is ready for reading.  It is suggested to attempt a
     * "direct" tansfer.
     */
    DirectResult (*direct)(int fd, enum istream_direct fd_type, void *ctx);

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
     * @return #WRITE_BROKEN to continue reading from the socket (by
     * returning #WRITE_BROKEN), #WRITE_ERRNO if the caller shall
     * close the socket with the error, #WRITE_DESTROYED if the
     * function has destroyed the #BufferedSocket
     */
    enum write_result (*broken)(void *ctx);

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
struct BufferedSocket {
    SocketWrapper base;

    const struct timeval *read_timeout, *write_timeout;

    /**
     * Postpone ScheduleRead(), calls Read().
     */
    struct defer_event defer_read;

    const BufferedSocketHandler *handler;
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

    BufferedResult last_buffered_result;
#endif

    void Init(struct pool *_pool,
              int _fd, enum istream_direct _fd_type,
              const struct timeval *_read_timeout,
              const struct timeval *_write_timeout,
              const BufferedSocketHandler *_handler, void *_ctx);

    /**
     * Close the physical socket, but do not destroy the input buffer.  To
     * do the latter, call buffered_socket_destroy().
     */
    void Close() {
        assert(!ended);
        assert(!destroyed);

        defer_event_cancel(&defer_read);
        base.Close();
    }

    /**
     * Just like buffered_socket_close(), but do not actually close the
     * socket.  The caller is responsible for closing the socket (or
     * scheduling it for reuse).
     */
    void Abandon() {
        assert(!ended);
        assert(!destroyed);

        defer_event_cancel(&defer_read);
        base.Abandon();
    }

    /**
     * Destroy the object.  Prior to that, the socket must be removed by
     * calling either buffered_socket_close() or
     * buffered_socket_abandon().
     */
    void Destroy();

    /**
     * Is the object still usable?  The socket may be closed already, but
     * the input buffer may still have data.
     */
    bool IsValid() const {
        /* the object is valid if there is either a valid socket or a
           buffer that may have more data; in the latter case, the socket
           may be closed already because no more data is needed from
           there */
        return base.IsValid() || input != nullptr;
    }

    /**
     * Is the socket still connected?  This does not actually check
     * whether the socket is connected, just whether it is known to be
     * closed.
     */
    bool IsConnected() const {
        assert(!destroyed);

        return base.IsValid();
    }

    /**
     * Returns the socket descriptor and calls buffered_socket_abandon().
     * Returns -1 if the input buffer is not empty.
     */
    int AsFD();

    /**
     * Is the input buffer empty?
     */
    gcc_pure
    bool IsEmpty() const;

    /**
     * Is the input buffer full?
     */
    gcc_pure
    bool IsFull() const;

    /**
     * Returns the number of bytes in the input buffer.
     */
    gcc_pure
    size_t GetAvailable() const;

    /**
     * Mark the specified number of bytes of the input buffer as
     * "consumed".  Call this in the data() method.  Note that this method
     * does not invalidate the buffer passed to data().  It may be called
     * repeatedly.
     */
    void Consumed(size_t nbytes);

    /**
     * Returns the istream_direct mask for splicing data into this socket.
     */
    istream_direct GetDirectMask() const {
        assert(!ended);
        assert(!destroyed);

        return base.GetDirectMask();
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
    bool Read(bool expect_more);

    void SetCork(bool cork) {
        base.SetCork(cork);
    }

    /**
     * Write data to the socket.
     *
     * @return the positive number of bytes written or a #write_result
     * code
     */
    ssize_t Write(const void *data, size_t length);

    /**
     * Transfer data from the given file descriptor to the socket.
     *
     * @return the positive number of bytes transferred or a #write_result
     * code
     */
    ssize_t WriteFrom(int other_fd, enum istream_direct other_fd_type,
                      size_t length);

    gcc_pure
    bool IsReadyForWriting() const {
        assert(!destroyed);

        return base.IsReadyForWriting();
    }

    void ScheduleReadTimeout(bool _expect_more,
                             const struct timeval *timeout);

    /**
     * Schedules reading on the socket with timeout disabled, to indicate
     * that you are willing to read, but do not expect it yet.  No direct
     * action is taken.  Use this to enable reading when you are still
     * sending the request.  When you are finished sending the request,
     * you should call BufferedSocket::Read() to enable the read timeout.
     */
    void ScheduleReadNoTimeout(bool _expect_more) {
        ScheduleReadTimeout(_expect_more, nullptr);
    }

    void ScheduleWrite() {
        assert(!ended);
        assert(!destroyed);

        base.ScheduleWrite(write_timeout);
    }

    void UnscheduleWrite() {
        assert(!ended);
        assert(!destroyed);

        base.UnscheduleWrite();
    }
};

gcc_const
static inline GQuark
buffered_socket_quark(void)
{
    return g_quark_from_static_string("buffered_socket");
}

#endif
