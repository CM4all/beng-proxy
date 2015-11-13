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
#include "istream/istream_pointer.hxx"
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

class WasOutput final : IstreamHandler {
public:
    struct pool &pool;

    const int fd;
    Event event;

    const WasOutputHandler &handler;
    void *handler_ctx;

    IstreamPointer input;

    uint64_t sent = 0;

    bool known_length = false;

    WasOutput(struct pool &p, int _fd, Istream &_input,
              const WasOutputHandler &_handler, void *_handler_ctx)
        :pool(p), fd(_fd),
         handler(_handler), handler_ctx(_handler_ctx),
         input(_input, *this, ISTREAM_TO_PIPE) {
        event.Set(fd, EV_WRITE|EV_TIMEOUT,
                  MakeEventCallback(WasOutput, EventCallback), this);
        ScheduleWrite();
    }

    void ScheduleWrite() {
        event.Add(was_output_timeout);
    }

    void AbortError(GError *error) {
        event.Delete();

        if (input.IsDefined())
            input.ClearAndClose();

        handler.abort(error, handler_ctx);
    }

    void EventCallback(evutil_socket_t fd, short events);

    /* virtual methods from class IstreamHandler */
    size_t OnData(const void *data, size_t length) override;
    ssize_t OnDirect(FdType type, int fd, size_t max_length) override;
    void OnEof() override;
    void OnError(GError *error) override;
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
    assert(input.IsDefined());

    if (unlikely(events & EV_TIMEOUT)) {
        GError *error = g_error_new_literal(was_quark(), 0, "send timeout");
        AbortError(error);
        return;
    }

    if (!known_length) {
        off_t available = input.GetAvailable(false);
        if (available != -1) {
            known_length = true;
            if (!handler.length(sent + available, handler_ctx))
                return;
        }
    }

    input.Read();

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
    assert(input.IsDefined());

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
        nbytes = istream_direct_to_pipe(type, source_fd, fd, max_length);
    }

    return nbytes;
}

inline void
WasOutput::OnEof()
{
    assert(input.IsDefined());

    input.Clear();
    event.Delete();

    if (!known_length && !handler.length(sent, handler_ctx))
        return;

    handler.eof(handler_ctx);
}

inline void
WasOutput::OnError(GError *error)
{
    assert(input.IsDefined());

    input.Clear();
    event.Delete();

    handler.premature(sent, error, handler_ctx);
}

/*
 * constructor
 *
 */

WasOutput *
was_output_new(struct pool &pool, int fd, Istream &input,
               const WasOutputHandler &handler, void *handler_ctx)
{
    assert(fd >= 0);
    assert(handler.length != nullptr);
    assert(handler.premature != nullptr);
    assert(handler.eof != nullptr);
    assert(handler.abort != nullptr);

    return NewFromPool<WasOutput>(pool, pool, fd, input,
                                  handler, handler_ctx);
}

uint64_t
was_output_free(WasOutput *output)
{
    assert(output != nullptr);

    if (output->input.IsDefined())
        output->input.ClearAndClose();

    output->event.Delete();

    return output->sent;
}
