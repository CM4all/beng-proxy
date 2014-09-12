/*
 * Wrapper for a socket file descriptor.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SOCKET_WRAPPER_HXX
#define BENG_PROXY_SOCKET_WRAPPER_HXX

#include "istream-direct.h"
#include "pevent.h"

#include <inline/compiler.h>

#include <event.h>
#include <sys/types.h>
#include <assert.h>
#include <stddef.h>

struct fifo_buffer;

struct socket_handler {
    /**
     * The socket is ready for reading.
     *
     * @return false when the socket has been closed
     */
    bool (*read)(void *ctx);

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
};

class SocketWrapper {
    struct pool *pool;

    int fd;
    enum istream_direct fd_type;

    enum istream_direct direct_mask;

    struct event read_event, write_event;

    const struct socket_handler *handler;
    void *handler_ctx;

public:
    void Init(struct pool *_pool,
              int _fd, enum istream_direct _fd_type,
              const struct socket_handler *_handler, void *_ctx);

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

    struct pool *GetPool() {
        return pool;
    }

    bool IsValid() const {
        return fd >= 0;
    }

    int GetFD() const {
        return fd;
    }

    istream_direct GetType() const {
        return fd_type;
    }

    /**
     * Returns the istream_direct mask for splicing data into this socket.
     */
    enum istream_direct GetDirectMask() const {
        assert(IsValid());

        return direct_mask;
    }

    void ScheduleRead(const struct timeval *timeout) {
        assert(IsValid());

        if (timeout == nullptr && event_pending(&read_event, EV_TIMEOUT, nullptr))
            /* work around libevent bug: event_add() should disable the
               timeout if tv==nullptr, but in fact it does not; workaround:
               delete the whole event first, then re-add it */
            p_event_del(&read_event, pool);

        p_event_add(&read_event, timeout, pool, "socket_read");
    }

    void UnscheduleRead() {
        p_event_del(&read_event, pool);
    }

    void ScheduleWrite(const struct timeval *timeout) {
        assert(IsValid());

        if (timeout == nullptr &&
            event_pending(&write_event, EV_TIMEOUT, nullptr))
            /* work around libevent bug: event_add() should disable the
               timeout if tv==nullptr, but in fact it does not; workaround:
               delete the whole event first, then re-add it */
            p_event_del(&write_event, pool);

        p_event_add(&write_event, timeout, pool, "socket_write");
    }

    void UnscheduleWrite() {
        p_event_del(&write_event, pool);
    }

    gcc_pure
    bool IsReadPending() const {
        return event_pending(&read_event, EV_READ, nullptr);
    }

    gcc_pure
    bool IsWritePending() const {
        return event_pending(&write_event, EV_WRITE, nullptr);
    }

    ssize_t ReadToBuffer(struct fifo_buffer *buffer, size_t length);

    void SetCork(bool cork);

    gcc_pure
    bool IsReadyForWriting() const;

    ssize_t Write(const void *data, size_t length);

    ssize_t WriteFrom(int other_fd, enum istream_direct other_fd_type,
                      size_t length);

private:
    static void ReadEventCallback(int _fd, short event, void *ctx);
    static void WriteEventCallback(int _fd, short event, void *ctx);
};

#endif
