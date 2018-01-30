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

#include "istream_gb.hxx"
#include "istream/istream.hxx"
#include "istream/Bucket.hxx"
#include "istream/UnusedPtr.hxx"
#include "GrowingBuffer.hxx"
#include "util/ConstBuffer.hxx"
#include "util/Cast.hxx"

#include <algorithm>

class GrowingBufferIstream final : public Istream {
    GrowingBufferReader reader;

public:
    GrowingBufferIstream(struct pool &p, GrowingBuffer &&_gb)
        :Istream(p), reader(std::move(_gb)) {
        _gb.Release();
    }

    /* virtual methods from class Istream */

    off_t _GetAvailable(gcc_unused bool partial) noexcept override {
        return reader.Available();
    }

    off_t _Skip(off_t _nbytes) noexcept override {
        size_t nbytes = _nbytes > off_t(reader.Available())
            ? reader.Available()
            : size_t(_nbytes);

        reader.Skip(nbytes);
        Consumed(nbytes);
        return nbytes;
    }

    void _Read() noexcept override {
        /* this loop is required to cross the buffer borders */
        while (true) {
            auto src = reader.Read();
            if (src.IsNull()) {
                assert(reader.IsEOF());
                DestroyEof();
                return;
            }

            assert(!reader.IsEOF());

            size_t nbytes = InvokeData(src.data, src.size);
            if (nbytes == 0)
                /* growing_buffer has been closed */
                return;

            reader.Consume(nbytes);
            if (nbytes < src.size)
                return;
        }
    }

    void _FillBucketList(IstreamBucketList &list) override {
        reader.FillBucketList(list);
    }

    size_t _ConsumeBucketList(size_t nbytes) noexcept override {
        size_t consumed = reader.ConsumeBucketList(nbytes);
        Consumed(consumed);
        return consumed;
    }
};

UnusedIstreamPtr
istream_gb_new(struct pool &pool, GrowingBuffer &&gb)
{
    return UnusedIstreamPtr(NewIstream<GrowingBufferIstream>(pool, std::move(gb)));
}
