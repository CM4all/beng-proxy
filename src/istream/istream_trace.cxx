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

#include "istream_trace.hxx"
#include "ForwardIstream.hxx"
#include "UnusedPtr.hxx"
#include "Bucket.hxx"
#include "New.hxx"
#include "pool/pool.hxx"
#include "util/Exception.hxx"

#include <stdio.h>

class TraceIstream final : public ForwardIstream {
public:
    TraceIstream(struct pool &_pool, UnusedIstreamPtr &&_input)
        :ForwardIstream(_pool, std::move(_input)) {
        fprintf(stderr, "%p new()\n", (const void *)this);
    }

    /* virtual methods from class Istream */

    off_t _GetAvailable(bool partial) noexcept override {
        fprintf(stderr, "%p available(%d)\n", (const void *)this, partial);
        auto available = ForwardIstream::_GetAvailable(partial);
        fprintf(stderr, "%p available(%d)=%ld\n", (const void *)this,
                partial, (long)available);

        return available;
    }

    off_t _Skip(off_t length) noexcept override {
        fprintf(stderr, "%p skip(0x%lu)\n", (const void *)this,
                (unsigned long)length);

        auto result = ForwardIstream::_Skip(length);

        fprintf(stderr, "%p skip(0x%lu) = %lu\n", (const void *)this,
                (unsigned long)length, (unsigned long)result);
        return result;
    }

    void _Read() noexcept override {
        fprintf(stderr, "%p read(0x%x)\n", (const void *)this,
                GetHandlerDirect());

        ForwardIstream::_Read();
    }

    void _FillBucketList(IstreamBucketList &list) override {
        fprintf(stderr, "%p fill(0x%x)\n", (const void *)this,
                GetHandlerDirect());

        IstreamBucketList tmp;

        try {
            input.FillBucketList(tmp);
        } catch (...) {
            fprintf(stderr, "%p fill error '%s'\n", (const void *)this,
                    GetFullMessage(std::current_exception()).c_str());
            Destroy();
            throw;
        }

        fprintf(stderr, "%p fill=%zu more=%d\n", (const void *)this,
                tmp.GetTotalBufferSize(), tmp.HasMore());
        list.SpliceBuffersFrom(tmp);
    }

    int _AsFd() noexcept override {
        auto fd = ForwardIstream::_AsFd();
        fprintf(stderr, "%p as_fd()=%d\n", (const void *)this, fd);
        return fd;
    }

    void _Close() noexcept override {
        fprintf(stderr, "%p close()\n", (const void *)this);

        ForwardIstream::_Close();
    }

    /* virtual methods from class IstreamHandler */

    size_t OnData(const void *data, size_t length) noexcept override {
        fprintf(stderr, "%p data(%zu)\n", (const void *)this, length);
        TraceData(data, length);
        auto nbytes = ForwardIstream::OnData(data, length);
        fprintf(stderr, "%p data(%zu)=%zu\n",
                (const void *)this, length, nbytes);
        return nbytes;
    }

    ssize_t OnDirect(FdType type, int fd, size_t max_length) noexcept override {
        fprintf(stderr, "%p direct(0x%x, %zd)\n", (const void *)this,
                GetHandlerDirect(), max_length);
        auto nbytes = ForwardIstream::OnDirect(type, fd, max_length);
        fprintf(stderr, "%p direct(0x%x, %zd)=%zd\n", (const void *)this,
                GetHandlerDirect(), max_length, nbytes);

        return nbytes;
    }

    void OnEof() noexcept override {
        fprintf(stderr, "%p eof()\n", (const void *)this);

        ForwardIstream::OnEof();
    }

    void OnError(std::exception_ptr ep) noexcept override {
        fprintf(stderr, "%p abort('%s')\n", (const void *)this,
                GetFullMessage(ep).c_str());

        ForwardIstream::OnError(ep);
    }

private:
    static void TraceData(const void *data, size_t length);
};

inline void
TraceIstream::TraceData(const void *data0, size_t length)
{
    const char *data = (const char *)data0;
    size_t i;

    fputc('"', stderr);
    for (i = 0; i < length; ++i) {
        if (data[i] == '\n')
            fputs("\\n", stderr);
        else if (data[i] == '\r')
            fputs("\\r", stderr);
        else if (data[i] == 0)
            fputs("\\0", stderr);
        else if (data[i] == '"')
            fputs("\\\"", stderr);
        else
            fputc(data[i], stderr);
    }
    fputs("\"\n", stderr);
}

/*
 * constructor
 *
 */

UnusedIstreamPtr
istream_trace_new(struct pool &pool, UnusedIstreamPtr input) noexcept
{
    return NewIstreamPtr<TraceIstream>(pool, std::move(input));
}
