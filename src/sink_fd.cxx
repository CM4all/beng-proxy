/*
 * An istream handler which sends data to a socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "sink_fd.hxx"
#include "pool.hxx"
#include "pevent.h"
#include "direct.h"
#include "fd-util.h"
#include "istream.h"

#include <event.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>

struct sink_fd {
    struct pool *pool;

    struct istream *input;

    int fd;
    enum istream_direct fd_type;
    const struct sink_fd_handler *handler;
    void *handler_ctx;

    struct event event;

    /**
     * Set to true each time data was received from the istream.
     */
    bool got_data;

    /**
     * This flag is used to determine if the EV_WRITE event shall be
     * scheduled after a splice().  We need to add the event only if
     * the splice() was triggered by EV_WRITE, because then we're
     * responsible for querying more data.
     */
    bool got_event;

#ifndef NDEBUG
    bool valid;
#endif
};

static void
sink_fd_schedule_write(struct sink_fd *ss)
{
    assert(ss != nullptr);
    assert(ss->fd >= 0);
    assert(ss->input != nullptr);

    ss->got_event = false;

    p_event_add(&ss->event, nullptr, ss->pool, "sink_fd");
}

/*
 * istream handler
 *
 */

static size_t
sink_fd_data(const void *data, size_t length, void *ctx)
{
    struct sink_fd *ss = (struct sink_fd *)ctx;

    ss->got_data = true;

    ssize_t nbytes = (ss->fd_type & ISTREAM_ANY_SOCKET) != 0
        ? send(ss->fd, data, length, MSG_DONTWAIT|MSG_NOSIGNAL)
        : write(ss->fd, data, length);
    if (nbytes >= 0) {
        sink_fd_schedule_write(ss);
        return nbytes;
    } else if (errno == EAGAIN) {
        sink_fd_schedule_write(ss);
        return 0;
    } else {
        p_event_del(&ss->event, ss->pool);
        if (ss->handler->send_error(errno, ss->handler_ctx))
            istream_close(ss->input);
        return 0;
    }
}

static ssize_t
sink_fd_direct(enum istream_direct type, int fd, size_t max_length, void *ctx)
{
    struct sink_fd *ss = (struct sink_fd *)ctx;

    ss->got_data = true;

    ssize_t nbytes = istream_direct_to(fd, type, ss->fd, ss->fd_type,
                                       max_length);
    if (unlikely(nbytes < 0 && errno == EAGAIN)) {
        if (!fd_ready_for_writing(ss->fd)) {
            sink_fd_schedule_write(ss);
            return ISTREAM_RESULT_BLOCKING;
        }

        /* try again, just in case connection->fd has become ready
           between the first istream_direct_to_socket() call and
           fd_ready_for_writing() */
        nbytes = istream_direct_to(fd, type, ss->fd, ss->fd_type, max_length);
    }

    if (likely(nbytes > 0) && (ss->got_event || type == ISTREAM_FILE))
        /* regular files don't have support for EV_READ, and thus the
           sink is responsible for triggering the next splice */
        sink_fd_schedule_write(ss);

    return nbytes;
}

static void
sink_fd_eof(void *ctx)
{
    struct sink_fd *ss = (struct sink_fd *)ctx;

    ss->got_data = true;

#ifndef NDEBUG
    ss->valid = false;
#endif

    p_event_del(&ss->event, ss->pool);

    ss->handler->input_eof(ss->handler_ctx);
}

static void
sink_fd_abort(GError *error, void *ctx)
{
    struct sink_fd *ss = (struct sink_fd *)ctx;

    ss->got_data = true;

#ifndef NDEBUG
    ss->valid = false;
#endif

    p_event_del(&ss->event, ss->pool);

    ss->handler->input_error(error, ss->handler_ctx);
}

static const struct istream_handler sink_fd_handler = {
    .data = sink_fd_data,
    .direct = sink_fd_direct,
    .eof = sink_fd_eof,
    .abort = sink_fd_abort,
};

/*
 * libevent callback
 *
 */

static void
socket_event_callback(gcc_unused int fd, gcc_unused short event,
                      void *ctx)
{
    struct sink_fd *ss = (struct sink_fd *)ctx;

    assert(fd == ss->fd);

    pool_ref(ss->pool);

    ss->got_event = true;
    ss->got_data = false;
    istream_read(ss->input);

    if (!ss->got_data)
        /* the fd is ready for writing, but the istream is blocking -
           don't try again for now */
        p_event_del(&ss->event, ss->pool);

    pool_unref(ss->pool);
    pool_commit();
}

/*
 * constructor
 *
 */

struct sink_fd *
sink_fd_new(struct pool *pool, struct istream *istream,
                int fd, enum istream_direct fd_type,
                const struct sink_fd_handler *handler, void *ctx)
{
    assert(pool != nullptr);
    assert(istream != nullptr);
    assert(fd >= 0);
    assert(handler != nullptr);
    assert(handler->input_eof != nullptr);
    assert(handler->input_error != nullptr);
    assert(handler->send_error != nullptr);

    auto ss = NewFromPool<struct sink_fd>(*pool);
    ss->pool = pool;

    istream_assign_handler(&ss->input, istream,
                           &sink_fd_handler, ss,
                           istream_direct_mask_to(fd_type));

    ss->fd = fd;
    ss->fd_type = fd_type;
    ss->handler = handler;
    ss->handler_ctx = ctx;

    event_set(&ss->event, fd, EV_WRITE|EV_PERSIST, socket_event_callback, ss);
    sink_fd_schedule_write(ss);

    ss->got_event = false;

#ifndef NDEBUG
    ss->valid = true;
#endif

    return ss;
}

void
sink_fd_read(struct sink_fd *ss)
{
    assert(ss != nullptr);
    assert(ss->valid);
    assert(ss->input != nullptr);

    istream_read(ss->input);
}

void
sink_fd_close(struct sink_fd *ss)
{
    assert(ss != nullptr);
    assert(ss->valid);
    assert(ss->input != nullptr);

#ifndef NDEBUG
    ss->valid = false;
#endif

    p_event_del(&ss->event, ss->pool);
    istream_close(ss->input);
}
