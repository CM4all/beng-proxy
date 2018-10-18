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

#include "duplex.hxx"
#include "event/SocketEvent.hxx"
#include "event/DeferEvent.hxx"
#include "system/Error.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "net/Buffered.hxx"
#include "io/Buffered.hxx"
#include "io/Logger.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "pool/pool.hxx"
#include "fb_pool.hxx"
#include "SliceFifoBuffer.hxx"

#include <sys/socket.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

class FallbackEvent {
    SocketEvent socket_event;
    DeferEvent defer_event;

    const unsigned events;

    const BoundMethod<void()> callback;

public:
    FallbackEvent(EventLoop &event_loop, FileDescriptor fd, unsigned _events,
                  BoundMethod<void()> _callback)
        :socket_event(event_loop, BIND_THIS_METHOD(OnSocket),
                      SocketDescriptor::FromFileDescriptor(fd)),
         defer_event(event_loop, _callback),
         events(fd.IsRegularFile() ? 0 : _events),
         callback(_callback) {}

    void Add() {
        if (events == 0)
            /* if "fd" is a regular file, trigger the event repeatedly
               using DeferEvent, because we can't use SocketEvent::READ on
               regular files */
            defer_event.Schedule();
        else
            socket_event.Schedule(events);
    }

    void Delete() {
        socket_event.Cancel();
        defer_event.Cancel();
    }

private:
    void OnSocket(unsigned) {
        callback();
    }
};

class Duplex {
    UniqueFileDescriptor read_fd;
    UniqueFileDescriptor write_fd;
    UniqueSocketDescriptor sock_fd;
    bool sock_eof = false;

    SliceFifoBuffer from_read, to_write;

    FallbackEvent read_event, write_event;
    SocketEvent socket_event;

public:
    Duplex(EventLoop &event_loop,
           UniqueFileDescriptor &&_read_fd, UniqueFileDescriptor &&_write_fd,
           UniqueSocketDescriptor &&_sock_fd)
        :read_fd(std::move(_read_fd)), write_fd(std::move(_write_fd)),
         sock_fd(std::move(_sock_fd)),
         read_event(event_loop, read_fd, SocketEvent::READ,
                    BIND_THIS_METHOD(ReadEventCallback)),
         write_event(event_loop, write_fd, SocketEvent::WRITE,
                     BIND_THIS_METHOD(WriteEventCallback)),
         socket_event(event_loop, BIND_THIS_METHOD(OnSocketReady), sock_fd)
    {
        from_read.Allocate(fb_pool_get());
        to_write.Allocate(fb_pool_get());

        read_event.Add();
        socket_event.ScheduleRead();
    }

private:
    void CloseRead() {
        assert(read_fd.IsDefined());

        read_event.Delete();

        if (read_fd.Get() > 2)
            read_fd.Close();
        else
            read_fd.Steal();
    }

    void CloseWrite() {
        assert(write_fd.IsDefined());

        write_event.Delete();

        if (write_fd.Get() > 2)
            write_fd.Close();
        else
            write_fd.Steal();
    }

    void CloseSocket() {
        assert(sock_fd.IsDefined());

        socket_event.Cancel();

        sock_fd.Close();
    }

    void Destroy();
    bool CheckDestroy();

    void ReadEventCallback();
    void WriteEventCallback();

    bool TryReadSocket() noexcept;
    bool TryWriteSocket() noexcept;
    void OnSocketReady(unsigned events) noexcept;
};

void
Duplex::Destroy()
{
    if (read_fd.IsDefined())
        CloseRead();

    if (write_fd.IsDefined())
        CloseWrite();

    if (sock_fd.IsDefined())
        CloseSocket();

    from_read.Free();
    to_write.Free();

    this->~Duplex();
}

bool
Duplex::CheckDestroy()
{
    if (!read_fd.IsDefined() && sock_eof &&
        from_read.empty() && to_write.empty()) {
        Destroy();
        return true;
    } else
        return false;
}

inline void
Duplex::ReadEventCallback()
{
    ssize_t nbytes = read_to_buffer(read_fd.Get(), from_read, INT_MAX);
    if (nbytes == -1) {
        LogConcat(1, "Duplex", "failed to read: ", strerror(errno));
        Destroy();
        return;
    }

    if (nbytes == 0) {
        CloseRead();
        CheckDestroy();
        return;
    }

    socket_event.ScheduleWrite();

    if (from_read.IsFull())
        read_event.Delete();
}

inline void
Duplex::WriteEventCallback()
{
    ssize_t nbytes = write_from_buffer(write_fd.Get(), to_write);
    if (nbytes == -1) {
        Destroy();
        return;
    }

    if (nbytes > 0 && !sock_eof)
        socket_event.ScheduleRead();

    if (to_write.empty()) {
        write_event.Delete();
        CheckDestroy();
    }
}

inline bool
Duplex::TryReadSocket() noexcept
{
    ssize_t nbytes = ReceiveToBuffer(sock_fd.Get(), to_write);
    if (nbytes == -1) {
        LogConcat(1, "Duplex", "failed to read: ", strerror(errno));
        Destroy();
        return false;
    }

    if (gcc_likely(nbytes > 0)) {
        write_event.Add();
        if (to_write.IsFull())
            socket_event.CancelRead();
        return true;
    } else {
        socket_event.CancelRead();
        sock_eof = true;
        return !CheckDestroy();
    }
}

inline bool
Duplex::TryWriteSocket() noexcept
{
    ssize_t nbytes = SendFromBuffer(sock_fd.Get(), from_read);
    if (nbytes == -1) {
        Destroy();
        return false;
    }

    if (nbytes > 0 && read_fd.IsDefined())
        read_event.Add();

    if (!from_read.empty())
        socket_event.ScheduleWrite();

    return true;
}

inline void
Duplex::OnSocketReady(unsigned events) noexcept
{
    if (events & SocketEvent::READ) {
        if (!TryReadSocket())
            return;
    }

    if (events & SocketEvent::WRITE)
        TryWriteSocket();
}

UniqueSocketDescriptor
duplex_new(EventLoop &event_loop, struct pool *pool,
           UniqueFileDescriptor read_fd, UniqueFileDescriptor write_fd)
{
    assert(pool != nullptr);
    assert(read_fd.IsDefined());
    assert(write_fd.IsDefined());

    UniqueSocketDescriptor result_fd, duplex_fd;
    if (!UniqueSocketDescriptor::CreateSocketPairNonBlock(AF_LOCAL, SOCK_STREAM, 0,
                                                          result_fd, duplex_fd))
        throw MakeErrno("socketpair() failed");

    NewFromPool<Duplex>(*pool, event_loop,
                        std::move(read_fd), std::move(write_fd),
                        std::move(duplex_fd));
    return result_fd;
}
