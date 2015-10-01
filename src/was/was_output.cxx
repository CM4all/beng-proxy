/*
 * Web Application Socket protocol, output data channel library.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "was_output.hxx"
#include "was_quark.h"
#include "event/Event.hxx"
#include "event/Callback.hxx"
#include "direct.hxx"
#include "system/fd-util.h"
#include "istream/istream_oo.hxx"
#include "pool.hxx"

#include <daemon/log.h>
#include <was/protocol.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>

static constexpr struct timeval was_output_timeout = {
    .tv_sec = 120,
    .tv_usec = 0,
};

class WasOutput {
public:
    struct pool *pool;

    int fd;
    Event event;

    const WasOutputHandler *handler;
    void *handler_ctx;

    struct istream *input;

    uint64_t sent;

    bool known_length;

    void ScheduleWrite() {
        event.Add(was_output_timeout);
    }

    void AbortError(GError *error) {
        event.Delete();

        if (input != nullptr)
            istream_free_handler(&input);

        handler->abort(error, handler_ctx);
    }

    void EventCallback(evutil_socket_t fd, short events);

    /* istream handler */

    size_t OnData(const void *data, size_t length);
    ssize_t OnDirect(FdType type, int fd, size_t max_length);
    void OnEof();
    void OnError(GError *error);
};

/*
 * libevent callback
 *
 */

inline void
WasOutput::EventCallback(gcc_unused evutil_socket_t _fd, short events)
{
    assert(_fd == fd);
    assert(fd >= 0);
    assert(input != nullptr);

    if (unlikely(events & EV_TIMEOUT)) {
        GError *error = g_error_new_literal(was_quark(), 0, "send timeout");
        AbortError(error);
        return;
    }

    if (!known_length) {
        off_t available = istream_available(input, false);
        if (available != -1) {
            known_length = true;
            if (!handler->length(sent + available, handler_ctx))
                return;
        }
    }

    istream_read(input);

    pool_commit();
}


/*
 * istream handler for the request
 *
 */

inline size_t
WasOutput::OnData(const void *p, size_t length)
{
    assert(fd >= 0);
    assert(input != nullptr);

    ssize_t nbytes = write(fd, p, length);
    if (likely(nbytes > 0)) {
        sent += nbytes;
        ScheduleWrite();
    } else if (nbytes < 0) {
        if (errno == EAGAIN) {
            ScheduleWrite();
            return 0;
        }

        GError *error = g_error_new(was_quark(), errno,
                                    "data write failed: %s", strerror(errno));
        AbortError(error);
        return 0;
    }

    return (size_t)nbytes;
}

inline ssize_t
WasOutput::OnDirect(FdType type, int source_fd, size_t max_length)
{
    assert(fd >= 0);

    ssize_t nbytes = istream_direct_to_pipe(type, source_fd, fd, max_length);
    if (likely(nbytes > 0)) {
        sent += nbytes;
        ScheduleWrite();
    } else if (nbytes < 0 && errno == EAGAIN) {
        if (!fd_ready_for_writing(fd)) {
            ScheduleWrite();
            return ISTREAM_RESULT_BLOCKING;
        }

        /* try again, just in case fd has become ready between
           the first istream_direct_to_pipe() call and
           fd_ready_for_writing() */
        nbytes = istream_direct_to_pipe(type, fd, fd, max_length);
    }

    return nbytes;
}

inline void
WasOutput::OnEof()
{
    assert(input != nullptr);

    input = nullptr;
    event.Delete();

    if (!known_length && !handler->length(sent, handler_ctx))
        return;

    handler->eof(handler_ctx);
}

inline void
WasOutput::OnError(GError *error)
{
    assert(input != nullptr);

    input = nullptr;
    event.Delete();

    handler->premature(sent, error, handler_ctx);
}

/*
 * constructor
 *
 */

WasOutput *
was_output_new(struct pool *pool, int fd, struct istream *input,
               const WasOutputHandler *handler, void *handler_ctx)
{
    assert(fd >= 0);
    assert(input != nullptr);
    assert(handler != nullptr);
    assert(handler->length != nullptr);
    assert(handler->premature != nullptr);
    assert(handler->eof != nullptr);
    assert(handler->abort != nullptr);

    auto output = NewFromPool<WasOutput>(*pool);
    output->pool = pool;
    output->fd = fd;
    output->event.Set(output->fd, EV_WRITE|EV_TIMEOUT,
                      MakeEventCallback(WasOutput, EventCallback), output);

    output->handler = handler;
    output->handler_ctx = handler_ctx;

    istream_assign_handler(&output->input, input,
                           &MakeIstreamHandler<WasOutput>::handler, output,
                           ISTREAM_TO_PIPE);

    output->sent = 0;
    output->known_length = false;

    output->ScheduleWrite();

    return output;
}

uint64_t
was_output_free(WasOutput *output)
{
    assert(output != nullptr);

    if (output->input != nullptr)
        istream_free_handler(&output->input);

    output->event.Delete();

    return output->sent;
}
