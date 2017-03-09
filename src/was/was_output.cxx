/*
 * Web Application Socket protocol, output data channel library.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "was_output.hxx"
#include "was_quark.h"
#include "event/SocketEvent.hxx"
#include "direct.hxx"
#include "io/FileDescriptor.hxx"
#include "istream/Pointer.hxx"
#include "pool.hxx"

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
    FileDescriptor fd;
    SocketEvent event;

    WasOutputHandler &handler;

    IstreamPointer input;

    uint64_t sent = 0;

    bool known_length = false;

    WasOutput(EventLoop &event_loop, FileDescriptor _fd, Istream &_input,
              WasOutputHandler &_handler)
        :fd(_fd),
         event(event_loop, fd.Get(), EV_WRITE,
               BIND_THIS_METHOD(WriteEventCallback)),
         handler(_handler),
         input(_input, *this, ISTREAM_TO_PIPE) {
        ScheduleWrite();
    }

    void ScheduleWrite() {
        event.Add(was_output_timeout);
    }

    void AbortError(GError *error) {
        event.Delete();

        if (input.IsDefined())
            input.ClearAndClose();

        handler.WasOutputError(error);
    }

    bool CheckLength();

    void WriteEventCallback(unsigned events);

    /* virtual methods from class IstreamHandler */
    size_t OnData(const void *data, size_t length) override;
    ssize_t OnDirect(FdType type, int fd, size_t max_length) override;
    void OnEof() override;
    void OnError(GError *error) override;
};

bool
WasOutput::CheckLength()
{
    if (known_length)
        return true;

    off_t available = input.GetAvailable(false);
    if (available < 0)
        return true;

    known_length = true;
    return handler.WasOutputLength(sent + available);
}

/*
 * libevent callback
 *
 */

inline void
WasOutput::WriteEventCallback(unsigned events)
{
    assert(fd.IsDefined());
    assert(input.IsDefined());

    if (unlikely(events & EV_TIMEOUT)) {
        GError *error = g_error_new_literal(was_quark(), 0, "send timeout");
        AbortError(error);
        return;
    }

    if (CheckLength())
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
    assert(fd.IsDefined());
    assert(input.IsDefined());

    ssize_t nbytes = fd.Write(p, length);
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
    assert(fd.IsDefined());

    ssize_t nbytes = istream_direct_to_pipe(type, source_fd,
                                            fd.Get(), max_length);
    if (likely(nbytes > 0)) {
        sent += nbytes;
        ScheduleWrite();
    } else if (nbytes < 0 && errno == EAGAIN) {
        if (!fd.IsReadyForWriting()) {
            ScheduleWrite();
            return ISTREAM_RESULT_BLOCKING;
        }

        /* try again, just in case fd has become ready between
           the first istream_direct_to_pipe() call and
           fd.IsReadyForWriting() */
        nbytes = istream_direct_to_pipe(type, source_fd, fd.Get(), max_length);
    }

    return nbytes;
}

inline void
WasOutput::OnEof()
{
    assert(input.IsDefined());

    input.Clear();
    event.Delete();

    if (!known_length && !handler.WasOutputLength(sent))
        return;

    handler.WasOutputEof();
}

inline void
WasOutput::OnError(GError *error)
{
    assert(input.IsDefined());

    input.Clear();
    event.Delete();

    handler.WasOutputPremature(sent, error);
}

/*
 * constructor
 *
 */

WasOutput *
was_output_new(struct pool &pool, EventLoop &event_loop,
               FileDescriptor fd, Istream &input,
               WasOutputHandler &handler)
{
    assert(fd.IsDefined());

    return NewFromPool<WasOutput>(pool, event_loop, fd, input, handler);
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

bool
was_output_check_length(WasOutput &output)
{
    return output.CheckLength();
}
