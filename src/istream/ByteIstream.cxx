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

#include "ByteIstream.hxx"
#include "ForwardIstream.hxx"
#include "UnusedPtr.hxx"
#include "Bucket.hxx"
#include "New.hxx"

class ByteIstream final : public ForwardIstream {
public:
    ByteIstream(struct pool &p, UnusedIstreamPtr _input)
        :ForwardIstream(p, std::move(_input)) {}

    /* virtual methods from class Istream */

    off_t _GetAvailable(gcc_unused bool partial) noexcept override {
        return -1;
    }

    off_t _Skip(gcc_unused off_t length) noexcept override {
        return -1;
    }

    void _FillBucketList(IstreamBucketList &list) override {
        IstreamBucketList tmp;

        try {
            input.FillBucketList(tmp);
        } catch (...) {
            Destroy();
            throw;
        }

        list.SpliceBuffersFrom(tmp, 1);
    }

    int _AsFd() noexcept override {
        return -1;
    }

    /* handler */

    size_t OnData(const void *data, gcc_unused size_t length) noexcept override {
        return ForwardIstream::OnData(data, 1);
    }

    ssize_t OnDirect(FdType type, int fd,
                     gcc_unused size_t max_length) noexcept override {
        return ForwardIstream::OnDirect(type, fd, 1);
    }
};

UnusedIstreamPtr
istream_byte_new(struct pool &pool, UnusedIstreamPtr input)
{
    return UnusedIstreamPtr(NewIstream<ByteIstream>(pool, std::move(input)));
}
