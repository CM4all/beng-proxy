/*
 * An istream handler which sends data to a socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "sink-socket.h"
#include "pevent.h"
#include "direct.h"
#include "fd-util.h"

#include <event.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>

struct sink_socket {
    struct pool *pool;

    struct istream *input;

    int fd;
    enum istream_direct fd_type;
    const struct sink_socket_handler *handler;
    void *handler_ctx;

    struct event event;
};

static void
sink_socket_schedule_write(struct sink_socket *ss)
{
    assert(ss != NULL);
    assert(ss->fd >= 0);
    assert(ss->input != NULL);

    p_event_add(&ss->event, NULL, ss->pool, "sink_socket");
}

/*
 * istream handler
 *
 */

static size_t
sink_socket_data(const void *data, size_t length, void *ctx)
{
    struct sink_socket *ss = ctx;

    ssize_t nbytes = send(ss->fd, data, length, MSG_DONTWAIT|MSG_NOSIGNAL);
    if (nbytes >= 0) {
        sink_socket_schedule_write(ss);
        return nbytes;
    } else if (errno == EAGAIN) {
        sink_socket_schedule_write(ss);
        return 0;
    } else {
        if (ss->handler->send_error(errno, ss->handler_ctx))
            istream_close(ss->input);
        return 0;
    }
}

static ssize_t
sink_socket_direct(istream_direct_t type, int fd, size_t max_length, void *ctx)
{
    struct sink_socket *ss = ctx;

    ssize_t nbytes = istream_direct_to_socket(type, fd, ss->fd, max_length);
    if (unlikely(nbytes < 0 && errno == EAGAIN)) {
        if (!fd_ready_for_writing(ss->fd)) {
            sink_socket_schedule_write(ss);
            return -2;
        }

        /* try again, just in case connection->fd has become ready
           between the first istream_direct_to_socket() call and
           fd_ready_for_writing() */
        nbytes = istream_direct_to_socket(type, fd, ss->fd, max_length);
    }

    if (likely(nbytes > 0) && type == ISTREAM_FILE)
        /* regular files don't have support for EV_READ, and thus the
           sink is responsible for triggering the next splice */
        sink_socket_schedule_write(ss);

    return nbytes;
}

static void
sink_socket_eof(void *ctx)
{
    struct sink_socket *ss = ctx;

    p_event_del(&ss->event, ss->pool);

    ss->handler->input_eof(ss->handler_ctx);
}

static void
sink_socket_abort(GError *error, void *ctx)
{
    struct sink_socket *ss = ctx;

    p_event_del(&ss->event, ss->pool);

    ss->handler->input_error(error, ss->handler_ctx);
}

static const struct istream_handler sink_socket_handler = {
    .data = sink_socket_data,
    .direct = sink_socket_direct,
    .eof = sink_socket_eof,
    .abort = sink_socket_abort,
};

/*
 * libevent callback
 *
 */

static void
socket_event_callback(G_GNUC_UNUSED int fd, G_GNUC_UNUSED short event,
                      void *ctx)
{
    struct sink_socket *ss = ctx;

    assert(fd == ss->fd);

    istream_read(ss->input);

    pool_commit();
}

/*
 * constructor
 *
 */

struct sink_socket *
sink_socket_new(struct pool *pool, struct istream *istream,
                int fd, enum istream_direct fd_type,
                const struct sink_socket_handler *handler, void *ctx)
{
    assert(pool != NULL);
    assert(istream != NULL);
    assert(fd >= 0);
    assert(handler != NULL);
    assert(handler->input_eof != NULL);
    assert(handler->input_error != NULL);
    assert(handler->send_error != NULL);

    struct sink_socket *ss = p_malloc(pool, sizeof(*ss));
    ss->pool = pool;

    istream_assign_handler(&ss->input, istream,
                           &sink_socket_handler, ss, ISTREAM_TO_SOCKET);

    ss->fd = fd;
    ss->fd_type = fd_type;
    ss->handler = handler;
    ss->handler_ctx = ctx;

    event_set(&ss->event, fd, EV_WRITE, socket_event_callback, ss);
    sink_socket_schedule_write(ss);

    return ss;
}

void
sink_socket_read(struct sink_socket *ss)
{
    assert(ss != NULL);
    assert(ss->input != NULL);

    istream_read(ss->input);
}

void
sink_socket_close(struct sink_socket *ss)
{
    assert(ss != NULL);
    assert(ss->input != NULL);

    p_event_del(&ss->event, ss->pool);
    istream_close(ss->input);
}
