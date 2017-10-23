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

#ifndef BENG_PROXY_FILTERED_SOCKET_LEASE_HXX
#define BENG_PROXY_FILTERED_SOCKET_LEASE_HXX

#include "filtered_socket.hxx"
#include "lease.hxx"

/**
 * Wrapper for a #FilteredSocket which may be released at some point.
 * After that, remaining data in the input buffer can still be read.
 */
class FilteredSocketLease {
    FilteredSocket socket;
    struct lease_ref lease_ref;

public:
    FilteredSocketLease(EventLoop &event_loop,
                        SocketDescriptor fd, FdType fd_type,
                        Lease &lease,
                        const struct timeval *read_timeout,
                        const struct timeval *write_timeout,
                        const SocketFilter *filter, void *filter_ctx,
                        BufferedSocketHandler &handler)
        :socket(event_loop)
    {
        socket.Init(fd, fd_type, read_timeout, write_timeout,
                    filter, filter_ctx,
                    handler);
        lease_ref.Set(lease);
    }

    ~FilteredSocketLease() {
        assert(IsReleased());

        socket.Destroy();
    }

    EventLoop &GetEventLoop() {
        return socket.GetEventLoop();
    }

    gcc_pure
    bool IsValid() const {
        return socket.IsValid();
    }

    gcc_pure
    bool IsConnected() const {
        return socket.IsConnected();
    }

    gcc_pure
    bool HasFilter() const {
        assert(!IsReleased());

        return socket.HasFilter();
    }

#ifndef NDEBUG
    gcc_pure
    bool HasEnded() const {
        assert(!IsReleased());

        return socket.ended;
    }
#endif

    void Release(bool reuse) {
        socket.Abandon();
        lease_ref.Release(reuse);
    }

#ifndef NDEBUG
    bool IsReleased() const {
        return lease_ref.released;
    }
#endif

    gcc_pure
    FdType GetType() const {
        assert(!IsReleased());

        return socket.GetType();
    }

    void SetDirect(bool _direct) {
        assert(!IsReleased());

        socket.SetDirect(_direct);
    }

    int AsFD() {
        assert(!IsReleased());

        return socket.AsFD();
    }

    gcc_pure
    bool IsEmpty() const {
        return socket.IsEmpty();
    }

    gcc_pure
    size_t GetAvailable() const {
        return socket.GetAvailable();
    }

    WritableBuffer<void> ReadBuffer() const {
        return socket.ReadBuffer();
    }

    void Consumed(size_t nbytes) {
        socket.Consumed(nbytes);
    }

    bool Read(bool expect_more) {
        return socket.Read(expect_more);
    }

    void ScheduleReadTimeout(bool expect_more, const struct timeval *timeout) {
        assert(!IsReleased());

        socket.ScheduleReadTimeout(expect_more, timeout);
    }

    void ScheduleReadNoTimeout(bool expect_more) {
        assert(!IsReleased());

        socket.ScheduleReadNoTimeout(expect_more);
    }

    ssize_t Write(const void *data, size_t size) {
        assert(!IsReleased());

        return socket.Write(data, size);
    }

    void ScheduleWrite() {
        assert(!IsReleased());

        socket.ScheduleWrite();
    }

    void UnscheduleWrite() {
        assert(!IsReleased());

        socket.UnscheduleWrite();
    }

    ssize_t WriteV(const struct iovec *v, size_t n) {
        assert(!IsReleased());

        return socket.WriteV(v, n);
    }

    ssize_t WriteFrom(int fd, FdType fd_type, size_t length) {
        assert(!IsReleased());

        return socket.WriteFrom(fd, fd_type, length);
    }
};

#endif
