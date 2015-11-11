/*
 * An istream handler which sends data to a socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "sink_fd.hxx"
#include "pool.hxx"
#include "pevent.hxx"
#include "direct.hxx"
#include "system/fd-util.h"
#include "event/Event.hxx"
#include "event/Callback.hxx"
#include "istream_pointer.hxx"

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>

struct SinkFd {
    struct pool *pool;

    IstreamPointer input;

    int fd;
    FdType fd_type;
    const SinkFdHandler *handler;
    void *handler_ctx;

    Event event;

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
    bool got_event = false;

#ifndef NDEBUG
    bool valid = true;
#endif

    SinkFd(struct pool &_pool, Istream &_istream,
           int _fd, FdType _fd_type,
           const SinkFdHandler &_handler, void *_handler_ctx)
        :pool(&_pool),
         input(_istream, MakeIstreamHandler<SinkFd>::handler, this,
               istream_direct_mask_to(fd_type)),
         fd(_fd), fd_type(_fd_type),
         handler(&_handler), handler_ctx(_handler_ctx) {
        ScheduleWrite();
    }

    void ScheduleWrite() {
        assert(fd >= 0);
        assert(input.IsDefined());

        got_event = false;
        event.Add();
    }

    void EventCallback();

    /* request istream handler */
    size_t OnData(const void *data, size_t length);
    ssize_t OnDirect(FdType type, int fd, size_t max_length);
    void OnEof();
    void OnError(GError *error);
};

/*
 * istream handler
 *
 */

inline size_t
SinkFd::OnData(const void *data, size_t length)
{
    got_data = true;

    ssize_t nbytes = IsAnySocket(fd_type)
        ? send(fd, data, length, MSG_DONTWAIT|MSG_NOSIGNAL)
        : write(fd, data, length);
    if (nbytes >= 0) {
        ScheduleWrite();
        return nbytes;
    } else if (errno == EAGAIN) {
        ScheduleWrite();
        return 0;
    } else {
        event.Delete();
        if (handler->send_error(errno, handler_ctx))
            input.Close();
        return 0;
    }
}

inline ssize_t
SinkFd::OnDirect(FdType type, gcc_unused int _fd, size_t max_length)
{
    got_data = true;

    ssize_t nbytes = istream_direct_to(fd, type, fd, fd_type,
                                       max_length);
    if (unlikely(nbytes < 0 && errno == EAGAIN)) {
        if (!fd_ready_for_writing(fd)) {
            ScheduleWrite();
            return ISTREAM_RESULT_BLOCKING;
        }

        /* try again, just in case connection->fd has become ready
           between the first istream_direct_to_socket() call and
           fd_ready_for_writing() */
        nbytes = istream_direct_to(fd, type, fd, fd_type, max_length);
    }

    if (likely(nbytes > 0) && (got_event || type == FdType::FD_FILE))
        /* regular files don't have support for EV_READ, and thus the
           sink is responsible for triggering the next splice */
        ScheduleWrite();

    return nbytes;
}

inline void
SinkFd::OnEof()
{
    got_data = true;

#ifndef NDEBUG
    valid = false;
#endif

    event.Delete();

    handler->input_eof(handler_ctx);
}

inline void
SinkFd::OnError(GError *error)
{
    got_data = true;

#ifndef NDEBUG
    valid = false;
#endif

    event.Delete();

    handler->input_error(error, handler_ctx);
}

/*
 * libevent callback
 *
 */

inline void
SinkFd::EventCallback()
{
    pool_ref(pool);

    got_event = true;
    got_data = false;
    input.Read();

    if (!got_data)
        /* the fd is ready for writing, but the istream is blocking -
           don't try again for now */
        event.Delete();

    pool_unref(pool);
    pool_commit();
}

/*
 * constructor
 *
 */

SinkFd *
sink_fd_new(struct pool &pool, Istream &istream,
            int fd, FdType fd_type,
            const SinkFdHandler &handler, void *ctx)
{
    assert(fd >= 0);
    assert(handler.input_eof != nullptr);
    assert(handler.input_error != nullptr);
    assert(handler.send_error != nullptr);

    return NewFromPool<SinkFd>(pool, pool, istream, fd, fd_type,
                               handler, ctx);
}

void
sink_fd_read(SinkFd *ss)
{
    assert(ss != nullptr);
    assert(ss->valid);
    assert(ss->input.IsDefined());

    ss->input.Read();
}

void
sink_fd_close(SinkFd *ss)
{
    assert(ss != nullptr);
    assert(ss->valid);
    assert(ss->input.IsDefined());

#ifndef NDEBUG
    ss->valid = false;
#endif

    ss->event.Delete();
    ss->input.Close();
}
