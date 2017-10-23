/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef BENG_PROXY_BUFFERED_SOCKET_HXX
#define BENG_PROXY_BUFFERED_SOCKET_HXX

#include "DefaultFifoBuffer.hxx"
#include "event/net/SocketWrapper.hxx"
#include "event/DeferEvent.hxx"
#include "util/DestructObserver.hxx"
#include "util/LeakDetector.hxx"

#include <exception>

/**
 * Wrapper for a socket file descriptor with input buffer management.
 */
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
     * The #BufferedSocket object has been closed by the handler.
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
     * The #BufferedSocket object has been closed by the handler.
     */
    CLOSED,

    /**
     * There was an I/O error on the socket and errno contains the
     * error code.  The caller will create a std::system_error and will
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
     * BufferedSocket::WriteFrom(), i.e. when there is a source file
     * descriptor.
     */
    WRITE_SOURCE_EOF = 0,

    /**
     * An I/O error has occurred, and errno is set.
     */
    WRITE_ERRNO = -1,

    /**
     * The destination socket blocks.  The #BufferedSocket library
     * has already scheduled the "write" event to resume writing
     * later.
     */
    WRITE_BLOCKING = -2,

    /**
     * The #BufferedSocket was destroyed inside the function call.
     */
    WRITE_DESTROYED = -3,

    /**
     * See BufferedSocketHandler::broken()
     */
    WRITE_BROKEN = -4,
};

struct BufferedSocketHandler {
    /**
     * Data has been read from the socket into the input buffer.  Call
     * BufferedSocket::Consumed() each time you consume data from the
     * given buffer.
     */
    BufferedResult (*data)(const void *buffer, size_t size, void *ctx);

    /**
     * The socket is ready for reading.  It is suggested to attempt a
     * "direct" tansfer.
     */
    DirectResult (*direct)(int fd, FdType fd_type, void *ctx);

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
     * passed to BufferedSocket::Write() was written to the socket.
     *
     * This method is not actually used by #BufferedSocket; it is
     * only implemented for #FilteredSocket.
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
     * is assumed that the #BufferedSocket object has been closed.
     *
     * @param e the exception that was caught
     */
    void (*error)(std::exception_ptr e, void *ctx);
};

/**
 * A wrapper for #SocketWrapper that manages an optional input buffer.
 *
 * The object can have the following states:
 *
 * - uninitialised
 *
 * - connected (after Init())
 *
 * - disconnected (after Close() or
 * Abandon()); in this state, the remaining data
 * from the input buffer will be delivered
 *
 * - ended (when the handler method BufferedSocketHandler::end() is
 * invoked)
 *
 * - destroyed (after Destroy())
 */
class BufferedSocket final : DestructAnchor, LeakDetector, SocketHandler {
    SocketWrapper base;

    const struct timeval *read_timeout, *write_timeout;

    /**
     * Postpone ScheduleRead(), calls Read().
     */
    DeferEvent defer_read;

    const BufferedSocketHandler *handler;
    void *handler_ctx;

    DefaultFifoBuffer input;

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

    bool destroyed = true;

#ifndef NDEBUG
    bool reading, ended;

    BufferedResult last_buffered_result;
#endif

public:
    explicit BufferedSocket(EventLoop &_event_loop)
        :base(_event_loop, *this),
         defer_read(_event_loop, BIND_THIS_METHOD(DeferReadCallback)) {}

    EventLoop &GetEventLoop() {
        return defer_read.GetEventLoop();
    }

    void Init(SocketDescriptor _fd, FdType _fd_type,
              const struct timeval *_read_timeout,
              const struct timeval *_write_timeout,
              const BufferedSocketHandler &_handler, void *_ctx);

    void Reinit(const struct timeval *_read_timeout,
                const struct timeval *_write_timeout,
                const BufferedSocketHandler &_handler, void *_ctx);

    /**
     * Move the socket from another #BufferedSocket instance.  This
     * disables scheduled events, moves the input buffer to this
     * instance and installs a new handler.
     */
    void Init(BufferedSocket &&src,
              const struct timeval *_read_timeout,
              const struct timeval *_write_timeout,
              const BufferedSocketHandler &_handler, void *_ctx);

    void Shutdown() {
        base.Shutdown();
    }

    /**
     * Close the physical socket, but do not destroy the input buffer.  To
     * do the latter, call Destroy().
     */
    void Close() {
        assert(!ended);
        assert(!destroyed);

        defer_read.Cancel();
        base.Close();
    }

    /**
     * Just like Close(), but do not actually close the
     * socket.  The caller is responsible for closing the socket (or
     * scheduling it for reuse).
     */
    void Abandon() {
        assert(!ended);
        assert(!destroyed);

        defer_read.Cancel();
        base.Abandon();
    }

    /**
     * Destroy the object.  Prior to that, the socket must be removed by
     * calling either Close() or Abandon().
     */
    void Destroy();

    /**
     * Is the object (already and) still usable?  That is, Init() was
     * called, but Destroy() was NOT called yet?  The socket may be closed
     * already, though.
     */
    bool IsValid() const {
        return !destroyed;
    }

#ifndef NDEBUG
    bool HasEnded() const {
        return ended;
    }
#endif

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
     * Called after we learn that the peer has closed the connection,
     * and no more data is available on the socket.  At this point,
     * our socket descriptor has not yet been closed.
     */
    bool ClosedByPeer();

    FdType GetType() const {
        return base.GetType();
    }

    void SetDirect(bool _direct) {
        direct = _direct;
    }

    /**
     * Returns the socket descriptor and calls Abandon().
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

    gcc_pure
    WritableBuffer<void> ReadBuffer() const {
        assert(!ended);

        return input.Read().ToVoid();
    }

    /**
     * Mark the specified number of bytes of the input buffer as
     * "consumed".  Call this in the data() method.  Note that this
     * method does not invalidate the buffer passed to
     * BufferedSocketHandler::data().  It may be called repeatedly.
     */
    void Consumed(size_t nbytes);

    /**
     * The caller wants to read more data from the socket.  There are four
     * possible outcomes: a call to BufferedSocketHandler::read(), a call
     * to BufferedSocketHandler::direct(), a call to
     * BufferedSocketHandler::error() or (if there is no data available
     * yet) an event gets scheduled and the function returns immediately.
     *
     * @param expect_more if true, generates an error if no more data can
     * be read (socket already shut down, buffer empty); if false, the
     * existing expect_more state is unmodified
     */
    bool Read(bool expect_more);

    /**
     * Variant of Write() which does not touch events and does not
     * invoke any callbacks.  It circumvents all the #BufferedSocket
     * features and invokes SocketWrapper::Write() directly.  Use this
     * in special cases when you want to push data to the socket right
     * before closing it.
     */
    ssize_t DirectWrite(const void *data, size_t length) {
        return base.Write(data, length);
    }

    /**
     * Write data to the socket.
     *
     * @return the positive number of bytes written or a #write_result
     * code
     */
    ssize_t Write(const void *data, size_t length);

    ssize_t WriteV(const struct iovec *v, size_t n);

    /**
     * Transfer data from the given file descriptor to the socket.
     *
     * @return the positive number of bytes transferred or a #write_result
     * code
     */
    ssize_t WriteFrom(int other_fd, FdType other_fd_type,
                      size_t length);

    gcc_pure
    bool IsReadyForWriting() const {
        assert(!destroyed);

        return base.IsReadyForWriting();
    }

    /**
     * Defer a call to Read().
     */
    void DeferRead(bool _expect_more);

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

    void UnscheduleRead() {
        base.UnscheduleRead();
        defer_read.Cancel();
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

private:
    void ClosedPrematurely();
    void Ended();

    BufferedResult InvokeData();
    bool SubmitFromBuffer();
    bool SubmitDirect();
    bool FillBuffer();
    bool TryRead2();
    bool TryRead();

    static bool OnWrite(void *ctx);
    static bool OnRead(void *ctx);
    static bool OnTimeout(void *ctx);
    static const struct socket_handler buffered_socket_handler;

    void DeferReadCallback() {
        Read(false);
    }

    /* virtual methods from class SocketHandler */
    bool OnSocketRead() override;
    bool OnSocketWrite() override;
    bool OnSocketTimeout() override;
};

#endif
