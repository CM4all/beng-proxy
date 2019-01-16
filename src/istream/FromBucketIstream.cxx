/*
 * Copyright 2007-2019 Content Management AG
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

#include "FromBucketIstream.hxx"
#include "Bucket.hxx"
#include "UnusedPtr.hxx"

FromBucketIstream::FromBucketIstream(struct pool &_pool,
                                     UnusedIstreamPtr &&_input) noexcept
    :ForwardIstream(_pool, std::move(_input)) {}

void
FromBucketIstream::_Read() noexcept
{
    IstreamBucketList list;
    input.FillBucketList(list);
    if (list.IsEmpty())
        return;

    const DestructObserver destructed(*this);
    size_t total = 0;

    /* submit each bucket to InvokeData() */
    for (const auto &i : list) {
        // TODO: support more buffer types once they're implemented
        assert(i.GetType() == IstreamBucket::Type::BUFFER);

        const auto buffer = i.GetBuffer();
        size_t consumed = InvokeData(buffer.data, buffer.size);
        if (consumed == 0 && destructed)
            return;

        assert(!destructed);

        total += consumed;

        if (consumed < buffer.size)
            break;
    }

    input.ConsumeBucketList(total);
}

void
FromBucketIstream::_FillBucketList(IstreamBucketList &list)
{
    input.FillBucketList(list);
}
