/*
 * Convert an input and an output pipe to a duplex socket.
 *
 * This code is used in the test cases to convert stdin/stdout to a
 * single socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "duplex.hxx"
#include "system/fd-util.h"
#include "system/fd_util.h"
#include "event/event2.h"
#include "event/Event.hxx"
#include "event/Callback.hxx"
#include "buffered_io.hxx"
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

struct Duplex {
    int read_fd;
    int write_fd;
    int sock_fd;
    bool sock_eof = false;

    SliceFifoBuffer from_read, to_write;

    Event read_event, write_event;
    struct event2 sock_event;

    Duplex(int _read_fd, int _write_fd, int _sock_fd)
        :read_fd(_read_fd), write_fd(_write_fd), sock_fd(_sock_fd) {
        from_read.Allocate(fb_pool_get());
        to_write.Allocate(fb_pool_get());

        read_event.Set(read_fd, EV_READ,
                       MakeSimpleEventCallback(Duplex, ReadEventCallback),
                       this);
        read_event.Add();

        write_event.Set(write_fd, EV_WRITE,
                        MakeSimpleEventCallback(Duplex, WriteEventCallback),
                        this);
        write_event.Add();

        event2_init(&sock_event, sock_fd,
                    MakeEventCallback(Duplex, SocketEventCallback), this,
                    nullptr);
        event2_persist(&sock_event);
        event2_set(&sock_event, EV_READ);
    }

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

        event2_set(&sock_event, 0);
        event2_commit(&sock_event);

        close(sock_fd);
        sock_fd = -1;
    }

    void Destroy();
    bool CheckDestroy();

    void ReadEventCallback();
    void WriteEventCallback();
    void SocketEventCallback(evutil_socket_t fd, short events);
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

    event2_or(&sock_event, EV_WRITE);

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
        event2_or(&sock_event, EV_READ);

    if (!to_write.IsEmpty())
        write_event.Add();
}

inline void
Duplex::SocketEventCallback(evutil_socket_t fd, short events)
{
    event2_lock(&sock_event);
    event2_occurred_persist(&sock_event, events);

    if ((events & EV_READ) != 0) {
        ssize_t nbytes = recv_to_buffer(fd, to_write, INT_MAX);
        if (nbytes == -1) {
            daemon_log(1, "failed to read: %s\n", strerror(errno));
            Destroy();
            return;
        }

        if (nbytes == 0) {
            sock_eof = true;
            if (CheckDestroy())
                return;
        }

        if (likely(nbytes > 0))
            write_event.Add();

        if (!to_write.IsFull())
            event2_or(&sock_event, EV_READ);
    }

    if ((events & EV_WRITE) != 0) {
        ssize_t nbytes = send_from_buffer(fd, from_read);
        if (nbytes == -1) {
            Destroy();
            return;
        }

        if (nbytes > 0 && read_fd >= 0)
            read_event.Add();

        if (!from_read.IsEmpty())
            event2_or(&sock_event, EV_WRITE);
    }

    event2_unlock(&sock_event);
}

int
duplex_new(struct pool *pool, int read_fd, int write_fd)
{
    assert(pool != nullptr);
    assert(read_fd >= 0);
    assert(write_fd >= 0);

    int fds[2];
    if (socketpair_cloexec(AF_UNIX, SOCK_STREAM, 0, fds) < 0)
        return -1;

    if (fd_set_nonblock(fds[1], 1) < 0) {
        int save_errno = errno;
        close(fds[0]);
        close(fds[1]);
        errno = save_errno;
        return -1;
    }

    NewFromPool<Duplex>(*pool, read_fd, write_fd, fds[0]);
    return fds[1];
}
