/*
 * Wrapper for a socket file descriptor.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "socket_wrapper.hxx"
#include "direct.h"
#include "buffered_io.hxx"
#include "fd-util.h"
#include "fd_util.h"
#include "pool.hxx"
#include "pevent.h"

#include <socket/util.h>

#include <unistd.h>
#include <sys/socket.h>

void
SocketWrapper::ReadEventCallback(gcc_unused int fd, short event, void *ctx)
{
    SocketWrapper *s = (SocketWrapper *)ctx;
    assert(s->IsValid());

    if (event & EV_TIMEOUT)
        s->handler->timeout(s->handler_ctx);
    else
        s->handler->read(s->handler_ctx);

    pool_commit();
}

void
SocketWrapper::WriteEventCallback(gcc_unused int fd, gcc_unused short event,
                                  void *ctx)
{
    SocketWrapper *s = (SocketWrapper *)ctx;
    assert(s->IsValid());

    if (event & EV_TIMEOUT)
        s->handler->timeout(s->handler_ctx);
    else
        s->handler->write(s->handler_ctx);

    pool_commit();
}

void
SocketWrapper::Init(struct pool &_pool,
                    int _fd, enum istream_direct _fd_type,
                    const struct socket_handler &_handler, void *_ctx)
{
    assert(_fd >= 0);
    assert(_handler.read != nullptr);
    assert(_handler.write != nullptr);

    pool = &_pool;
    fd = _fd;
    fd_type = _fd_type;
    direct_mask = istream_direct_mask_to(fd_type);

    event_set(&read_event, fd, EV_READ|EV_PERSIST|EV_TIMEOUT,
              ReadEventCallback, this);

    event_set(&write_event, fd, EV_WRITE|EV_PERSIST|EV_TIMEOUT,
              WriteEventCallback, this);

    handler = &_handler;
    handler_ctx = _ctx;
}

void
SocketWrapper::Init(struct pool &_pool,
                    SocketWrapper &&src,
                    const struct socket_handler &_handler, void *_ctx)
{
    Init(_pool, src.fd, src.fd_type, _handler, _ctx);
    src.Abandon();
}

void
SocketWrapper::Close()
{
    if (fd < 0)
        return;

    p_event_del(&read_event, pool);
    p_event_del(&write_event, pool);

    close(fd);
    fd = -1;
}

void
SocketWrapper::Abandon()
{
    assert(fd >= 0);

    p_event_del(&read_event, pool);
    p_event_del(&write_event, pool);

    fd = -1;
}

int
SocketWrapper::AsFD()
{
    assert(IsValid());

    const int result = dup_cloexec(fd);
    Abandon();
    return result;
}

ssize_t
SocketWrapper::ReadToBuffer(struct fifo_buffer &buffer, size_t length)
{
    assert(IsValid());

    return recv_to_buffer(fd, &buffer, length);
}

void
SocketWrapper::SetCork(bool cork)
{
    assert(IsValid());

    socket_set_cork(fd, cork);
}

bool
SocketWrapper::IsReadyForWriting() const
{
    assert(IsValid());

    return fd_ready_for_writing(fd);
}

ssize_t
SocketWrapper::Write(const void *data, size_t length)
{
    assert(IsValid());

    return send(fd, data, length, MSG_DONTWAIT|MSG_NOSIGNAL);
}

ssize_t
SocketWrapper::WriteFrom(int other_fd, enum istream_direct other_fd_type,
                         size_t length)
{
    return istream_direct_to_socket(other_fd_type, other_fd, fd, length);
}

