/*
 * Copyright 2007-2018 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "Output.hxx"
#include "Error.hxx"
#include "event/NewSocketEvent.hxx"
#include "event/TimerEvent.hxx"
#include "direct.hxx"
#include "io/Splice.hxx"
#include "io/FileDescriptor.hxx"
#include "system/Error.hxx"
#include "istream/Handler.hxx"
#include "istream/Pointer.hxx"
#include "istream/UnusedPtr.hxx"
#include "pool/pool.hxx"

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
    NewSocketEvent event;
    TimerEvent timeout_event;

    WasOutputHandler &handler;

    IstreamPointer input;

    uint64_t sent = 0;

    bool known_length = false;

    WasOutput(EventLoop &event_loop, FileDescriptor _fd,
              UnusedIstreamPtr _input,
              WasOutputHandler &_handler)
        :fd(_fd),
         event(event_loop, BIND_THIS_METHOD(WriteEventCallback),
               SocketDescriptor::FromFileDescriptor(fd)),
         timeout_event(event_loop, BIND_THIS_METHOD(OnTimeout)),
         handler(_handler),
         input(std::move(_input), *this, ISTREAM_TO_PIPE) {
        ScheduleWrite();
    }

    void ScheduleWrite() {
        event.ScheduleWrite();
        timeout_event.Add(was_output_timeout);
    }

    void AbortError(std::exception_ptr ep) {
        event.Cancel();
        timeout_event.Cancel();

        if (input.IsDefined())
            input.ClearAndClose();

        handler.WasOutputError(ep);
    }

    bool CheckLength();

    void WriteEventCallback(unsigned events);

    void OnTimeout() noexcept {
        AbortError(std::make_exception_ptr(WasError("send timeout")));
    }

    /* virtual methods from class IstreamHandler */
    size_t OnData(const void *data, size_t length) override;
    ssize_t OnDirect(FdType type, int fd, size_t max_length) override;
    void OnEof() noexcept override;
    void OnError(std::exception_ptr ep) noexcept override;
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
WasOutput::WriteEventCallback(unsigned)
{
    assert(fd.IsDefined());
    assert(input.IsDefined());

    event.CancelWrite();
    timeout_event.Cancel();

    if (CheckLength())
        input.Read();
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
    if (gcc_likely(nbytes > 0)) {
        sent += nbytes;
        ScheduleWrite();
    } else if (nbytes < 0) {
        if (errno == EAGAIN) {
            ScheduleWrite();
            return 0;
        }

        AbortError(std::make_exception_ptr(MakeErrno("Write to WAS process failed")));
        return 0;
    }

    return (size_t)nbytes;
}

inline ssize_t
WasOutput::OnDirect(gcc_unused FdType type, int source_fd, size_t max_length)
{
    assert(fd.IsDefined());

    ssize_t nbytes = SpliceToPipe(source_fd, fd.Get(), max_length);
    if (gcc_likely(nbytes > 0)) {
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
        nbytes = SpliceToPipe(source_fd, fd.Get(), max_length);
    }

    return nbytes;
}

void
WasOutput::OnEof() noexcept
{
    assert(input.IsDefined());

    input.Clear();
    event.Cancel();
    timeout_event.Cancel();

    if (!known_length && !handler.WasOutputLength(sent))
        return;

    handler.WasOutputEof();
}

void
WasOutput::OnError(std::exception_ptr ep) noexcept
{
    assert(input.IsDefined());

    input.Clear();
    event.Cancel();
    timeout_event.Cancel();

    handler.WasOutputPremature(sent, ep);
}

/*
 * constructor
 *
 */

WasOutput *
was_output_new(struct pool &pool, EventLoop &event_loop,
               FileDescriptor fd, UnusedIstreamPtr input,
               WasOutputHandler &handler)
{
    assert(fd.IsDefined());

    return NewFromPool<WasOutput>(pool, event_loop, fd,
                                  std::move(input), handler);
}

uint64_t
was_output_free(WasOutput *output)
{
    assert(output != nullptr);

    if (output->input.IsDefined())
        output->input.ClearAndClose();

    output->event.Cancel();
    output->timeout_event.Cancel();

    return output->sent;
}

bool
was_output_check_length(WasOutput &output)
{
    return output.CheckLength();
}
