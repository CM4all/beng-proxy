/*
 * Wrapper for a socket file descriptor.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SOCKET_WRAPPER_H
#define BENG_PROXY_SOCKET_WRAPPER_H

#include "istream-direct.h"
#include "pevent.h"

#include <inline/compiler.h>

#include <event.h>
#include <sys/types.h>
#include <assert.h>
#include <stddef.h>
#include <stdbool.h>

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

struct socket_wrapper {
    struct pool *pool;

    int fd;
    enum istream_direct fd_type;

    enum istream_direct direct_mask;

    struct event read_event, write_event;

    const struct socket_handler *handler;
    void *handler_ctx;
};

void
socket_wrapper_init(struct socket_wrapper *s, struct pool *pool,
                    int fd, enum istream_direct fd_type,
                    const struct socket_handler *handler, void *ctx);

void
socket_wrapper_close(struct socket_wrapper *s);

/**
 * Just like socket_wrapper_close(), but do not actually close the
 * socket.  The caller is responsible for closing the socket (or
 * scheduling it for reuse).
 */
void
socket_wrapper_abandon(struct socket_wrapper *s);

/**
 * Returns the socket descriptor and calls socket_wrapper_abandon().
 */
int
socket_wrapper_as_fd(struct socket_wrapper *s);

static inline bool
socket_wrapper_valid(const struct socket_wrapper *s)
{
    assert(s != NULL);

    return s->fd >= 0;
}

/**
 * Returns the istream_direct mask for splicing data into this socket.
 */
static inline enum istream_direct
socket_wrapper_direct_mask(const struct socket_wrapper *s)
{
    assert(socket_wrapper_valid(s));

    return s->direct_mask;
}

static inline void
socket_wrapper_schedule_read(struct socket_wrapper *s,
                             const struct timeval *timeout)
{
    assert(socket_wrapper_valid(s));

    p_event_add(&s->read_event, timeout, s->pool, "socket_read");
}

static inline void
socket_wrapper_unschedule_read(struct socket_wrapper *s)
{
    p_event_del(&s->read_event, s->pool);
}

static inline void
socket_wrapper_schedule_write(struct socket_wrapper *s,
                              const struct timeval *timeout)
{
    assert(socket_wrapper_valid(s));

    p_event_add(&s->write_event, timeout, s->pool, "socket_write");
}

static inline void
socket_wrapper_unschedule_write(struct socket_wrapper *s)
{
    p_event_del(&s->write_event, s->pool);
}

ssize_t
socket_wrapper_read_to_buffer(struct socket_wrapper *s,
                              struct fifo_buffer *buffer, size_t length);

void
socket_wrapper_set_cork(struct socket_wrapper *s, bool cork);

gcc_pure
bool
socket_wrapper_ready_for_writing(const struct socket_wrapper *s);

ssize_t
socket_wrapper_write(struct socket_wrapper *s,
                     const void *data, size_t length);

ssize_t
socket_wrapper_write_from(struct socket_wrapper *s,
                          int fd, enum istream_direct fd_type,
                          size_t length);

#endif
