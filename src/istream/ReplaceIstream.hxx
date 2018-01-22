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

#include "pool/SharedPtr.hxx"

#include <sys/types.h>

struct pool;
class Istream;
class UnusedIstreamPtr;
class ReplaceIstream;

class ReplaceIstreamControl {
    friend class ReplaceIstream;

    ReplaceIstream *replace;

public:
    explicit ReplaceIstreamControl(ReplaceIstream &_replace) noexcept
        :replace(&_replace) {}

    void Add(off_t start, off_t end, UnusedIstreamPtr contents) noexcept;

    /**
     * Extend the end position of the latest replacement.
     *
     * @param start the start value that was passed to
     * istream_replace_add()
     * @param end the new end position; it must not be smaller than the
     * current end position of the replacement
     */
    void Extend(off_t start, off_t end) noexcept;

    /**
     * Mark all source data until the given offset as "settled",
     * i.e. there will be no more substitutions before this offset.  It
     * allows this object to deliver data until this offset to its
     * handler.
     */
    void Settle(off_t offset) noexcept;

    void Finish() noexcept;
};

/**
 * Process CM4all commands in a HTML stream, e.g. embeddings.
 */
std::pair<UnusedIstreamPtr, SharedPoolPtr<ReplaceIstreamControl>>
istream_replace_new(struct pool &pool, UnusedIstreamPtr input);
