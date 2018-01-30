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

#include "istream_rubber.hxx"
#include "istream/istream.hxx"
#include "istream/Bucket.hxx"
#include "istream/UnusedPtr.hxx"
#include "rubber.hxx"
#include "util/ConstBuffer.hxx"

#include <algorithm>

#include <assert.h>
#include <stdint.h>

class RubberIstream final : public Istream {
    Rubber &rubber;
    const unsigned id;
    const bool auto_remove;

    size_t position;
    const size_t end;

public:
    RubberIstream(struct pool &p, Rubber &_rubber, unsigned _id,
                  size_t start, size_t _end,
                  bool _auto_remove)
        :Istream(p), rubber(_rubber), id(_id), auto_remove(_auto_remove),
         position(start), end(_end) {}

    ~RubberIstream() noexcept override {
        if (auto_remove)
            rubber.Remove(id);
    }

    /* virtual methods from class Istream */

    off_t _GetAvailable(gcc_unused bool partial) noexcept override {
        return end - position;
    }

    off_t _Skip(off_t nbytes) noexcept override {
        assert(position <= end);

        const size_t remaining = end - position;
        if (nbytes > off_t(remaining))
            nbytes = remaining;

        position += nbytes;
        Consumed(nbytes);
        return nbytes;
    }

    void _Read() noexcept override {
        assert(position <= end);

        const uint8_t *data = (const uint8_t *)rubber.Read(id);
        const size_t remaining = end - position;

        if (remaining > 0) {
            size_t nbytes = InvokeData(data + position, remaining);
            if (nbytes == 0)
                return;

            position += nbytes;
        }

        if (position == end)
            DestroyEof();
    }

    void _FillBucketList(IstreamBucketList &list) override {
        const uint8_t *data = (const uint8_t *)rubber.Read(id);
        const size_t remaining = end - position;

        if (remaining > 0)
            list.Push(ConstBuffer<void>(data + position, remaining));
    }

    size_t _ConsumeBucketList(size_t nbytes) noexcept override {
        const size_t remaining = end - position;
        size_t consumed = std::min(nbytes, remaining);
        position += consumed;
        Consumed(consumed);
        return consumed;
    }
};

UnusedIstreamPtr
istream_rubber_new(struct pool &pool, Rubber &rubber,
                   unsigned id, size_t start, size_t end,
                   bool auto_remove)
{
    assert(id > 0);
    assert(start <= end);

    return UnusedIstreamPtr(NewIstream<RubberIstream>(pool, rubber, id,
                                                      start, end, auto_remove));
}
