/*
 * Wrapper for a socket file descriptor.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "socket_wrapper.h"
#include "direct.h"
#include "buffered-io.h"
#include "fd-util.h"
#include "pool.h"
#include "pevent.h"

#include <socket/util.h>

#include <unistd.h>
#include <sys/socket.h>

static void
socket_read_event_callback(gcc_unused int fd, short event, void *ctx)
{
    struct socket_wrapper *s = ctx;

    p_event_consumed(&s->read_event, s->pool);

    if (event & EV_TIMEOUT)
        s->handler->timeout(s->handler_ctx);
    else
        s->handler->read(s->handler_ctx);

    pool_commit();
}

static void
socket_write_event_callback(gcc_unused int fd, gcc_unused short event,
                            void *ctx)
{
    struct socket_wrapper *s = ctx;

    p_event_consumed(&s->write_event, s->pool);

    if (event & EV_TIMEOUT)
        s->handler->timeout(s->handler_ctx);
    else
        s->handler->write(s->handler_ctx);

    pool_commit();
}

void
socket_wrapper_init(struct socket_wrapper *s, struct pool *pool,
                    int fd, enum istream_direct fd_type,
                    const struct timeval *read_timeout,
                    const struct timeval *write_timeout,
                    const struct socket_handler *handler, void *ctx)
{
    assert(s != NULL);
    assert(pool != NULL);
    assert(fd >= 0);
    assert(handler != NULL);
    assert(handler->read != NULL);
    assert(handler->write != NULL);
    assert(handler->timeout != NULL || (read_timeout == NULL &&
                                        write_timeout == NULL));

    s->pool = pool;
    s->fd = fd;
    s->fd_type = fd_type;
    s->direct_mask = istream_direct_mask_to(fd_type);

    event_set(&s->read_event, fd, EV_READ|EV_PERSIST,
              socket_read_event_callback, s);

    event_set(&s->write_event, fd, EV_WRITE|EV_PERSIST|EV_TIMEOUT,
              socket_write_event_callback, s);

    s->read_timeout = read_timeout;
    s->write_timeout = write_timeout;

    s->handler = handler;
    s->handler_ctx = ctx;
}

void
socket_wrapper_close(struct socket_wrapper *s)
{
    if (s->fd < 0)
        return;

    p_event_del(&s->read_event, s->pool);
    p_event_del(&s->write_event, s->pool);

    close(s->fd);
    s->fd = -1;
}

ssize_t
socket_wrapper_read_to_buffer(struct socket_wrapper *s,
                              struct fifo_buffer *buffer, size_t length)
{
    assert(socket_wrapper_valid(s));

    return recv_to_buffer(s->fd, buffer, length);
}

void
socket_wrapper_set_cork(struct socket_wrapper *s, bool cork)
{
    assert(socket_wrapper_valid(s));

    socket_set_cork(s->fd, cork);
}

bool
socket_wrapper_ready_for_writing(const struct socket_wrapper *s)
{
    assert(socket_wrapper_valid(s));

    return fd_ready_for_writing(s->fd);
}

ssize_t
socket_wrapper_write(struct socket_wrapper *s,
                     const void *data, size_t length)
{
    assert(socket_wrapper_valid(s));

    return send(s->fd, data, length, MSG_DONTWAIT|MSG_NOSIGNAL);
}

ssize_t
socket_wrapper_write_from(struct socket_wrapper *s,
                          int fd, enum istream_direct fd_type,
                          size_t length)
{
    return istream_direct_to_socket(fd_type, fd, s->fd, length);
}

