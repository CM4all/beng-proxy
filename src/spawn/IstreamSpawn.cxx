/*
 * Copyright 2007-2017 Content Management AG
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

#include "IstreamSpawn.hxx"
#include "spawn/Interface.hxx"
#include "spawn/Prepared.hxx"
#include "spawn/ExitListener.hxx"
#include "system/fd_util.h"
#include "istream/UnusedPtr.hxx"
#include "istream/istream.hxx"
#include "istream/Pointer.hxx"
#include "istream/Handler.hxx"
#include "io/Splice.hxx"
#include "io/Buffered.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "io/Logger.hxx"
#include "direct.hxx"
#include "event/SocketEvent.hxx"
#include "pool/pool.hxx"
#include "fb_pool.hxx"
#include "SliceFifoBuffer.hxx"
#include "system/Error.hxx"
#include "util/Cast.hxx"

#ifdef __linux
#include <fcntl.h>
#endif

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <signal.h>
#include <limits.h>

struct SpawnIstream final : Istream, IstreamHandler, ExitListener {
    const LLogger logger;
    SpawnService &spawn_service;

    UniqueFileDescriptor output_fd;
    SocketEvent output_event;

    SliceFifoBuffer buffer;

    IstreamPointer input;
    UniqueFileDescriptor input_fd;
    SocketEvent input_event;

    int pid;

    SpawnIstream(SpawnService &_spawn_service, EventLoop &event_loop,
                 struct pool &p,
                 UnusedIstreamPtr _input, UniqueFileDescriptor _input_fd,
                 UniqueFileDescriptor _output_fd,
                 pid_t _pid);

    bool CheckDirect() const {
        return Istream::CheckDirect(FdType::FD_PIPE);
    }

    void Cancel();

    void FreeBuffer() {
        buffer.FreeIfDefined(fb_pool_get());
    }

    /**
     * Send data from the buffer.  Invokes the "eof" callback when the
     * buffer becomes empty and the pipe has been closed already.
     *
     * @return true if the caller shall read more data from the pipe
     */
    bool SendFromBuffer();

    void ReadFromOutput();

    void InputEventCallback(unsigned) {
        input.Read();
    }

    void OutputEventCallback(unsigned) {
        ReadFromOutput();
    }

    /* virtual methods from class Istream */

    void _Read() noexcept override;
    // TODO: implement int AsFd() override;
    void _Close() noexcept override;

    /* virtual methods from class IstreamHandler */
    size_t OnData(const void *data, size_t length) override;
    ssize_t OnDirect(FdType type, int fd, size_t max_length) override;
    void OnEof() noexcept override;
    void OnError(std::exception_ptr ep) noexcept override;

    /* virtual methods from class ExitListener */
    void OnChildProcessExit(int status) override;
};

void
SpawnIstream::Cancel()
{
    assert(output_fd.IsDefined());

    if (input.IsDefined()) {
        assert(input_fd.IsDefined());

        input_event.Delete();
        input_fd.Close();
        input.Close();
    }

    output_event.Delete();

    output_fd.Close();

    if (pid >= 0) {
        spawn_service.KillChildProcess(pid);
        pid = -1;
    }
}

inline bool
SpawnIstream::SendFromBuffer()
{
    assert(buffer.IsDefined());

    if (Istream::SendFromBuffer(buffer) == 0)
        return false;

    if (!output_fd.IsDefined()) {
        if (buffer.IsEmpty()) {
            FreeBuffer();
            DestroyEof();
        }

        return false;
    }

    buffer.FreeIfEmpty(fb_pool_get());

    return true;
}

/*
 * input handler
 *
 */

inline size_t
SpawnIstream::OnData(const void *data, size_t length)
{
    assert(input_fd.IsDefined());

    ssize_t nbytes = input_fd.Write(data, length);
    if (nbytes > 0)
        input_event.Add();
    else if (nbytes < 0) {
        if (errno == EAGAIN) {
            input_event.Add();
            return 0;
        }

        logger(1, "write() to subprocess failed: ", strerror(errno));
        input_event.Delete();
        input_fd.Close();
        input.ClearAndClose();
        return 0;
    }

    return (size_t)nbytes;
}

inline ssize_t
SpawnIstream::OnDirect(gcc_unused FdType type, int fd, size_t max_length)
{
    assert(input_fd.IsDefined());

    ssize_t nbytes = SpliceToPipe(fd, input_fd.Get(), max_length);
    if (nbytes > 0)
        input_event.Add();
    else if (nbytes < 0) {
        if (errno == EAGAIN) {
            if (!input_fd.IsReadyForWriting()) {
                input_event.Add();
                return ISTREAM_RESULT_BLOCKING;
            }

            /* try again, just in case connection->fd has become ready
               between the first splice() call and
               fd_ready_for_writing() */
            nbytes = SpliceToPipe(fd, input_fd.Get(), max_length);
        }
    }

    return nbytes;
}

inline void
SpawnIstream::OnEof() noexcept
{
    assert(input.IsDefined());
    assert(input_fd.IsDefined());

    input_event.Delete();
    input_fd.Close();

    input.Clear();
}

void
SpawnIstream::OnError(std::exception_ptr ep) noexcept
{
    assert(input.IsDefined());
    assert(input_fd.IsDefined());

    FreeBuffer();

    input_event.Delete();
    input_fd.Close();
    input.Clear();

    Cancel();
    DestroyError(ep);
}

/*
 * event for fork.output_fd
 */

void
SpawnIstream::ReadFromOutput()
{
    assert(output_fd.IsDefined());

    if (!CheckDirect()) {
        buffer.AllocateIfNull(fb_pool_get());

        ssize_t nbytes = read_to_buffer(output_fd.Get(), buffer, INT_MAX);
        if (nbytes == -2) {
            /* XXX should not happen */
        } else if (nbytes > 0) {
            if (Istream::SendFromBuffer(buffer) > 0) {
                buffer.FreeIfEmpty(fb_pool_get());
                output_event.Add();
            }
        } else if (nbytes == 0) {
            Cancel();

            if (buffer.IsEmpty()) {
                FreeBuffer();
                DestroyEof();
            }
        } else if (errno == EAGAIN) {
            buffer.FreeIfEmpty(fb_pool_get());
            output_event.Add();

            if (input.IsDefined())
                /* the CGI may be waiting for more data from stdin */
                input.Read();
        } else {
            auto error = MakeErrno("failed to read from sub process");
            FreeBuffer();
            Cancel();
            DestroyError(std::make_exception_ptr(error));
        }
    } else {
        if (Istream::ConsumeFromBuffer(buffer) > 0)
            /* there's data left in the buffer, which must be consumed
               before we can switch to "direct" transfer */
            return;

        buffer.FreeIfDefined(fb_pool_get());

        /* at this point, the handler might have changed inside
           Istream::ConsumeFromBuffer(), and the new handler might not
           support "direct" transfer - check again */
        if (!CheckDirect()) {
            output_event.Add();
            return;
        }

        ssize_t nbytes = InvokeDirect(FdType::FD_PIPE, output_fd.Get(),
                                      INT_MAX);
        if (nbytes == ISTREAM_RESULT_BLOCKING ||
            nbytes == ISTREAM_RESULT_CLOSED) {
            /* -2 means the callback wasn't able to consume any data right
               now */
        } else if (nbytes > 0) {
            output_event.Add();
        } else if (nbytes == ISTREAM_RESULT_EOF) {
            FreeBuffer();
            Cancel();
            DestroyEof();
        } else if (errno == EAGAIN) {
            output_event.Add();

            if (input.IsDefined())
                /* the CGI may be waiting for more data from stdin */
                input.Read();
        } else {
            auto error = MakeErrno("failed to read from sub process");
            FreeBuffer();
            Cancel();
            DestroyError(std::make_exception_ptr(error));
        }
    }
}


/*
 * istream implementation
 *
 */

void
SpawnIstream::_Read() noexcept
{
    if (buffer.IsEmpty() || SendFromBuffer())
        ReadFromOutput();
}

void
SpawnIstream::_Close() noexcept
{
    FreeBuffer();

    if (output_fd.IsDefined())
        Cancel();

    Destroy();
}

/*
 * child callback
 *
 */

void
SpawnIstream::OnChildProcessExit(gcc_unused int status)
{
    assert(pid >= 0);

    pid = -1;
}


/*
 * constructor
 *
 */

inline
SpawnIstream::SpawnIstream(SpawnService &_spawn_service, EventLoop &event_loop,
                           struct pool &p,
                           UnusedIstreamPtr _input,
                           UniqueFileDescriptor _input_fd,
                           UniqueFileDescriptor _output_fd,
                           pid_t _pid)
    :Istream(p),
     logger("spawn"),
     spawn_service(_spawn_service),
     output_fd(std::move(_output_fd)),
     output_event(event_loop, output_fd.Get(), SocketEvent::READ,
                  BIND_THIS_METHOD(OutputEventCallback)),
     input(std::move(_input), *this, ISTREAM_TO_PIPE),
     input_fd(std::move(_input_fd)),
     input_event(event_loop, BIND_THIS_METHOD(InputEventCallback)),
     pid(_pid)
{
    if (input.IsDefined()) {
        input_event.Set(input_fd.Get(), SocketEvent::WRITE);
        input_event.Add();
    }

    spawn_service.SetExitListener(pid, this);
}

UnusedIstreamPtr
SpawnChildProcess(EventLoop &event_loop, struct pool *pool, const char *name,
                  UnusedIstreamPtr input,
                  PreparedChildProcess &&prepared,
                  SpawnService &spawn_service)
{
    if (input) {
        int fd = input.AsFd();
        if (fd >= 0)
            prepared.SetStdin(fd);
    }

    UniqueFileDescriptor stdin_pipe;
    if (input) {
        UniqueFileDescriptor stdin_r;
        if (!UniqueFileDescriptor::CreatePipe(stdin_r, stdin_pipe))
            throw MakeErrno("pipe() failed");

        prepared.SetStdin(std::move(stdin_r));

        stdin_pipe.SetNonBlocking();
    }

    UniqueFileDescriptor stdout_pipe, stdout_w;
    if (!UniqueFileDescriptor::CreatePipe(stdout_pipe, stdout_w))
        throw MakeErrno("pipe() failed");

    prepared.SetStdout(std::move(stdout_w));

    stdout_pipe.SetNonBlocking();

    const int pid = spawn_service.SpawnChildProcess(name, std::move(prepared),
                                                    nullptr);
    auto f = NewFromPool<SpawnIstream>(*pool, spawn_service, event_loop,
                                       *pool,
                                       std::move(input), std::move(stdin_pipe),
                                       std::move(stdout_pipe),
                                       pid);

    /* XXX CLOEXEC */

    return UnusedIstreamPtr(f);
}
