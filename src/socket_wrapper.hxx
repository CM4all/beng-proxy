/*
 * Wrapper for a socket file descriptor.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SOCKET_WRAPPER_HXX
#define BENG_PROXY_SOCKET_WRAPPER_HXX

#include "FdType.hxx"
#include "event/SocketEvent.hxx"
#include "net/SocketDescriptor.hxx"

#include <inline/compiler.h>

#include <sys/types.h>
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

template<typename T> class ForeignFifoBuffer;

class SocketHandler {
public:
    /**
     * The socket is ready for reading.
     *
     * @return false when the socket has been closed
     */
    virtual bool OnSocketRead() = 0;

    /**
     * The socket is ready for writing.
     *
     * @return false when the socket has been closed
     */
    virtual bool OnSocketWrite() = 0;

    /**
     * @return false when the socket has been closed
     */
    virtual bool OnSocketTimeout() = 0;
};

class SocketWrapper {
    SocketDescriptor fd;
    FdType fd_type;

    FdTypeMask direct_mask;

    SocketEvent read_event, write_event;

    SocketHandler *handler;

public:
    SocketWrapper(EventLoop &event_loop)
        :read_event(event_loop, BIND_THIS_METHOD(ReadEventCallback)),
         write_event(event_loop, BIND_THIS_METHOD(WriteEventCallback)) {}

    SocketWrapper(const SocketWrapper &) = delete;

    EventLoop &GetEventLoop() {
        return read_event.GetEventLoop();
    }

    void Init(int _fd, FdType _fd_type,
              SocketHandler &_handler);

    /**
     * Move the socket from another #SocketWrapper instance.  This
     * disables scheduled events and installs a new handler.
     */
    void Init(SocketWrapper &&src,
              SocketHandler &_handler);

    /**
     * Shut down the socket gracefully, allowing the TCP stack to
     * complete all pending transfers.  If you call Close() without
     * Shutdown(), it may reset the connection and discard pending
     * data.
     */
    void Shutdown();

    void Close();

    /**
     * Just like Close(), but do not actually close the
     * socket.  The caller is responsible for closing the socket (or
     * scheduling it for reuse).
     */
    void Abandon();

    /**
     * Returns the socket descriptor and calls socket_wrapper_abandon().
     */
    int AsFD();

    bool IsValid() const {
        return fd.IsDefined();
    }

    int GetFD() const {
        return fd.Get();
    }

    FdType GetType() const {
        return fd_type;
    }

    /**
     * Returns the FdTypeMask for splicing data into this socket.
     */
    FdTypeMask GetDirectMask() const {
        assert(IsValid());

        return direct_mask;
    }

    void ScheduleRead(const struct timeval *timeout) {
        assert(IsValid());

        if (timeout == nullptr && read_event.IsTimerPending())
            /* work around libevent bug: event_add() should disable the
               timeout if tv==nullptr, but in fact it does not; workaround:
               delete the whole event first, then re-add it */
            read_event.Delete();

        read_event.Add(timeout);
    }

    void UnscheduleRead() {
        read_event.Delete();
    }

    void ScheduleWrite(const struct timeval *timeout) {
        assert(IsValid());

        if (timeout == nullptr && write_event.IsTimerPending())
            /* work around libevent bug: event_add() should disable the
               timeout if tv==nullptr, but in fact it does not; workaround:
               delete the whole event first, then re-add it */
            write_event.Delete();

        write_event.Add(timeout);
    }

    void UnscheduleWrite() {
        write_event.Delete();
    }

    gcc_pure
    bool IsReadPending() const {
        return read_event.IsPending(EV_READ);
    }

    gcc_pure
    bool IsWritePending() const {
        return write_event.IsPending(EV_WRITE);
    }

    ssize_t ReadToBuffer(ForeignFifoBuffer<uint8_t> &buffer, size_t length);

    void SetCork(bool cork);

    gcc_pure
    bool IsReadyForWriting() const;

    ssize_t Write(const void *data, size_t length);

    ssize_t WriteV(const struct iovec *v, size_t n);

    ssize_t WriteFrom(int other_fd, FdType other_fd_type,
                      size_t length);

private:
    void ReadEventCallback(unsigned events);
    void WriteEventCallback(unsigned events);
};

#endif
