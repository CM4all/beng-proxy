/*
 * Convert an input and an output pipe to a duplex socket.
 *
 * This code is used in the test cases to convert stdin/stdout to a
 * single socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "duplex.h"
#include "socket-util.h"
#include "fifo-buffer.h"
#include "event2.h"
#include "buffered-io.h"

#include <inline/compiler.h>
#include <daemon/log.h>

#include <sys/socket.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

struct duplex {
    int read_fd;
    int write_fd;
    int sock_fd;
    bool sock_eof;
    fifo_buffer_t from_read;
    fifo_buffer_t to_write;

    struct event2 read_event, write_event, sock_event;
};

static void
duplex_close(struct duplex *duplex)
{
    if (duplex->read_fd >= 0) {
        event2_set(&duplex->read_event, 0);

        if (duplex->read_fd > 2)
            close(duplex->read_fd);

        duplex->read_fd = -1;
    }

    if (duplex->write_fd >= 0) {
        event2_set(&duplex->write_event, 0);

        if (duplex->write_fd > 2)
            close(duplex->write_fd);

        duplex->write_fd = -1;
    }

    if (duplex->sock_fd >= 0) {
        event2_set(&duplex->sock_event, 0);

        close(duplex->sock_fd);
        duplex->sock_fd = -1;
    }
}

static int
duplex_check_close(struct duplex *duplex)
{
    if (duplex->read_fd < 0 && duplex->sock_eof &&
        fifo_buffer_empty(duplex->from_read) &&
        fifo_buffer_empty(duplex->to_write)) {
        duplex_close(duplex);
        return 1;
    } else
        return 0;
}

static void
read_event_callback(int fd, short event __attr_unused, void *ctx)
{
    struct duplex *duplex = ctx;
    ssize_t nbytes;

    assert((event & EV_READ) != 0);

    event2_reset(&duplex->read_event);

    nbytes = read_to_buffer(fd, duplex->from_read, INT_MAX);

    if (nbytes == -1) {
        daemon_log(1, "failed to read: %s\n", strerror(errno));
        duplex_close(duplex);
        return;
    }

    if (nbytes == 0) {
        close(fd);
        duplex->read_fd = -1;
        if (duplex_check_close(duplex))
            return;
    }

    if (nbytes > 0)
        event2_or(&duplex->sock_event, EV_WRITE);

    if (duplex->read_fd >= 0 && !fifo_buffer_full(duplex->from_read))
        event2_or(&duplex->read_event, EV_READ);
}

static void
write_event_callback(int fd, short event __attr_unused, void *ctx)
{
    struct duplex *duplex = ctx;
    ssize_t nbytes;

    assert((event & EV_WRITE) != 0);

    event2_reset(&duplex->write_event);

    nbytes = write_from_buffer(fd, duplex->to_write);
    if (nbytes == -1) {
        duplex_close(duplex);
        return;
    }

    if (nbytes > 0)
        event2_or(&duplex->write_event, EV_WRITE);

    if (!duplex->sock_eof && !fifo_buffer_full(duplex->to_write))
        event2_or(&duplex->sock_event, EV_READ);
}

static void
sock_event_callback(int fd, short event, void *ctx)
{
    struct duplex *duplex = ctx;
    ssize_t nbytes;

    event2_reset(&duplex->sock_event);

    if ((event & EV_READ) != 0) {
        nbytes = recv_to_buffer(fd, duplex->to_write, INT_MAX);
        if (nbytes == -1) {
            daemon_log(1, "failed to read: %s\n", strerror(errno));
            duplex_close(duplex);
            return;
        }

        if (nbytes == 0) {
            duplex->sock_eof = true;
            if (duplex_check_close(duplex))
                return;
        }

        if (likely(nbytes > 0))
            event2_or(&duplex->write_event, EV_WRITE);
    }

    if (likely(!duplex->sock_eof && !fifo_buffer_full(duplex->to_write)))
        event2_or(&duplex->sock_event, EV_READ);

    if ((event & EV_WRITE) != 0) {
        nbytes = send_from_buffer(fd, duplex->from_read);
        if (nbytes == -1) {
            duplex_close(duplex);
            return;
        }

        if (nbytes > 0)
            event2_or(&duplex->sock_event, EV_WRITE);

        if (duplex->read_fd >= 0 && !fifo_buffer_full(duplex->from_read))
            event2_or(&duplex->read_event, EV_READ);
    } else {
        if (!fifo_buffer_empty(duplex->from_read))
            event2_or(&duplex->sock_event, EV_WRITE);
    }
}

int
duplex_new(pool_t pool, int read_fd, int write_fd)
{
    struct duplex *duplex;
    int ret, fds[2];

    assert(pool != NULL);
    assert(read_fd >= 0);
    assert(write_fd >= 0);

    ret = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    if (ret < 0)
        return -1;

    if (socket_set_nonblock(fds[1], 1) < 0) {
        int save_errno = errno;
        close(fds[0]);
        close(fds[1]);
        errno = save_errno;
        return -1;
    }

    duplex = p_malloc(pool, sizeof(*duplex));
    duplex->read_fd = read_fd;
    duplex->write_fd = write_fd;
    duplex->sock_fd = fds[0];
    duplex->from_read = fifo_buffer_new(pool, 4096);
    duplex->to_write = fifo_buffer_new(pool, 4096);
    duplex->sock_eof = false;

    event2_init(&duplex->read_event, read_fd,
                read_event_callback, duplex, NULL);
    event2_set(&duplex->read_event, EV_READ);

    event2_init(&duplex->write_event, write_fd,
                write_event_callback, duplex, NULL);

    event2_init(&duplex->sock_event, duplex->sock_fd,
                sock_event_callback, duplex, NULL);
    event2_set(&duplex->sock_event, EV_READ);

    return fds[1];
}
