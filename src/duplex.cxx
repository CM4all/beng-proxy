/*
 * Convert an input and an output pipe to a duplex socket.
 *
 * This code is used in the test cases to convert stdin/stdout to a
 * single socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "duplex.hxx"
#include "event/SocketEvent.hxx"
#include "event/DeferEvent.hxx"
#include "net/Buffered.hxx"
#include "io/Buffered.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "pool.hxx"
#include "fb_pool.hxx"
#include "SliceFifoBuffer.hxx"

#include <inline/compiler.h>
#include <daemon/log.h>

#include <sys/socket.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

class FallbackEvent {
    SocketEvent socket_event;
    DeferEvent defer_event;

    const BoundMethod<void()> callback;

public:
    FallbackEvent(EventLoop &event_loop, int fd, short events,
                  BoundMethod<void()> _callback)
        :socket_event(event_loop, fd, events, BIND_THIS_METHOD(OnSocket)),
         defer_event(event_loop, _callback),
         callback(_callback) {}

    void Add() {
        if (!socket_event.Add())
            /* if "fd" is a regular file, trigger the event repeatedly
               using DeferEvent, because we can't use EV_READ on
               regular files */
            defer_event.Schedule();
    }

    void Delete() {
        socket_event.Delete();
        defer_event.Cancel();
    }

private:
    void OnSocket(unsigned) {
        callback();
    }
};

class Duplex {
    int read_fd;
    int write_fd;
    int sock_fd;
    bool sock_eof = false;

    SliceFifoBuffer from_read, to_write;

    FallbackEvent read_event, write_event;
    SocketEvent socket_read_event, socket_write_event;

public:
    Duplex(EventLoop &event_loop, int _read_fd, int _write_fd, int _sock_fd)
        :read_fd(_read_fd), write_fd(_write_fd), sock_fd(_sock_fd),
         read_event(event_loop, read_fd, EV_READ,
                    BIND_THIS_METHOD(ReadEventCallback)),
         write_event(event_loop, write_fd, EV_WRITE,
                     BIND_THIS_METHOD(WriteEventCallback)),
         socket_read_event(event_loop, sock_fd, EV_READ,
                           BIND_THIS_METHOD(SocketReadEventCallback)),
         socket_write_event(event_loop, sock_fd, EV_WRITE,
                           BIND_THIS_METHOD(SocketWriteEventCallback))
    {
        from_read.Allocate(fb_pool_get());
        to_write.Allocate(fb_pool_get());

        read_event.Add();
        socket_read_event.Add();
    }

private:
    void CloseRead() {
        assert(read_fd >= 0);

        read_event.Delete();

        if (read_fd > 2)
            close(read_fd);

        read_fd = -1;
    }

    void CloseWrite() {
        assert(write_fd >= 0);

        write_event.Delete();

        if (write_fd > 2)
            close(write_fd);

        write_fd = -1;
    }

    void CloseSocket() {
        assert(sock_fd >= 0);

        socket_read_event.Delete();
        socket_write_event.Delete();

        close(sock_fd);
        sock_fd = -1;
    }

    void Destroy();
    bool CheckDestroy();

    void ReadEventCallback();
    void WriteEventCallback();
    void SocketReadEventCallback(unsigned);
    void SocketWriteEventCallback(unsigned);
};

void
Duplex::Destroy()
{
    if (read_fd >= 0)
        CloseRead();

    if (write_fd >= 0)
        CloseWrite();

    if (sock_fd >= 0)
        CloseSocket();

    from_read.Free(fb_pool_get());
    to_write.Free(fb_pool_get());
}

bool
Duplex::CheckDestroy()
{
    if (read_fd < 0 && sock_eof && from_read.IsEmpty() && to_write.IsEmpty()) {
        Destroy();
        return true;
    } else
        return false;
}

inline void
Duplex::ReadEventCallback()
{
    ssize_t nbytes = read_to_buffer(read_fd, from_read, INT_MAX);
    if (nbytes == -1) {
        daemon_log(1, "failed to read: %s\n", strerror(errno));
        Destroy();
        return;
    }

    if (nbytes == 0) {
        CloseRead();
        CheckDestroy();
        return;
    }

    socket_write_event.Add();

    if (!from_read.IsFull())
        read_event.Add();
}

inline void
Duplex::WriteEventCallback()
{
    ssize_t nbytes = write_from_buffer(write_fd, to_write);
    if (nbytes == -1) {
        Destroy();
        return;
    }

    if (nbytes > 0 && !sock_eof)
        socket_read_event.Add();

    if (!to_write.IsEmpty())
        write_event.Add();
    else
        CheckDestroy();
}

inline void
Duplex::SocketReadEventCallback(unsigned)
{
    ssize_t nbytes = ReceiveToBuffer(sock_fd, to_write);
    if (nbytes == -1) {
        daemon_log(1, "failed to read: %s\n", strerror(errno));
        Destroy();
        return;
    }

    if (likely(nbytes > 0)) {
        write_event.Add();
        if (!to_write.IsFull())
            socket_read_event.Add();
    } else {
        sock_eof = true;
        CheckDestroy();
    }
}

inline void
Duplex::SocketWriteEventCallback(unsigned)
{
    ssize_t nbytes = SendFromBuffer(sock_fd, from_read);
    if (nbytes == -1) {
        Destroy();
        return;
    }

    if (nbytes > 0 && read_fd >= 0)
        read_event.Add();

    if (!from_read.IsEmpty())
        socket_write_event.Add();
}

int
duplex_new(EventLoop &event_loop, struct pool *pool, int read_fd, int write_fd)
{
    assert(pool != nullptr);
    assert(read_fd >= 0);
    assert(write_fd >= 0);

    UniqueFileDescriptor result_fd, duplex_fd;
    if (!UniqueFileDescriptor::CreateSocketPairNonBlock(AF_LOCAL, SOCK_STREAM, 0,
                                                        result_fd, duplex_fd))
        return -1;

    NewFromPool<Duplex>(*pool, event_loop, read_fd, write_fd,
                        duplex_fd.Steal());
    return result_fd.Steal();
}
