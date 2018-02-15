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

#pragma once

#include "FilteredSocket.hxx"
#include "lease.hxx"

/**
 * Wrapper for a #FilteredSocket which may be released at some point.
 * After that, remaining data in the input buffer can still be read.
 */
class FilteredSocketLease {
    FilteredSocket socket;
    struct lease_ref lease_ref;

public:
    template<typename F>
    FilteredSocketLease(EventLoop &event_loop,
                        SocketDescriptor fd, FdType fd_type,
                        Lease &lease,
                        const struct timeval *read_timeout,
                        const struct timeval *write_timeout,
                        F &&filter,
                        BufferedSocketHandler &handler) noexcept
        :socket(event_loop)
    {
        socket.Init(fd, fd_type, read_timeout, write_timeout,
                    std::forward<F>(filter),
                    handler);
        lease_ref.Set(lease);
    }

    ~FilteredSocketLease() noexcept {
        assert(IsReleased());

        socket.Destroy();
    }

    EventLoop &GetEventLoop() noexcept {
        return socket.GetEventLoop();
    }

    gcc_pure
    bool IsConnected() const noexcept {
        return socket.IsConnected();
    }

    gcc_pure
    bool HasFilter() const noexcept {
        assert(!IsReleased());

        return socket.HasFilter();
    }

#ifndef NDEBUG
    gcc_pure
    bool HasEnded() const noexcept {
        assert(!IsReleased());

        return socket.ended;
    }
#endif

    void Release(bool reuse) noexcept {
        socket.Abandon();
        lease_ref.Release(reuse);
    }

#ifndef NDEBUG
    bool IsReleased() const noexcept {
        return lease_ref.released;
    }
#endif

    gcc_pure
    FdType GetType() const noexcept {
        assert(!IsReleased());

        return socket.GetType();
    }

    void SetDirect(bool _direct) noexcept {
        assert(!IsReleased());

        socket.SetDirect(_direct);
    }

    int AsFD() noexcept {
        assert(!IsReleased());

        return socket.AsFD();
    }

    gcc_pure
    bool IsEmpty() const noexcept {
        return socket.IsEmpty();
    }

    gcc_pure
    size_t GetAvailable() const noexcept {
        return socket.GetAvailable();
    }

    WritableBuffer<void> ReadBuffer() const noexcept {
        return socket.ReadBuffer();
    }

    void Consumed(size_t nbytes) noexcept {
        socket.Consumed(nbytes);
    }

    bool Read(bool expect_more) noexcept {
        return socket.Read(expect_more);
    }

    void ScheduleReadTimeout(bool expect_more,
                             const struct timeval *timeout) noexcept {
        assert(!IsReleased());

        socket.ScheduleReadTimeout(expect_more, timeout);
    }

    void ScheduleReadNoTimeout(bool expect_more) noexcept {
        assert(!IsReleased());

        socket.ScheduleReadNoTimeout(expect_more);
    }

    ssize_t Write(const void *data, size_t size) noexcept {
        assert(!IsReleased());

        return socket.Write(data, size);
    }

    void ScheduleWrite() noexcept {
        assert(!IsReleased());

        socket.ScheduleWrite();
    }

    void UnscheduleWrite() noexcept {
        assert(!IsReleased());

        socket.UnscheduleWrite();
    }

    ssize_t WriteV(const struct iovec *v, size_t n) noexcept {
        assert(!IsReleased());

        return socket.WriteV(v, n);
    }

    ssize_t WriteFrom(int fd, FdType fd_type, size_t length) noexcept {
        assert(!IsReleased());

        return socket.WriteFrom(fd, fd_type, length);
    }
};
