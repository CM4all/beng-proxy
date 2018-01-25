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

struct pool;
class Istream;
class UnusedIstreamPtr;
class OptionalIstream;

class OptionalIstreamControl {
    friend class OptionalIstream;

    OptionalIstream *optional;

public:
    explicit constexpr OptionalIstreamControl(OptionalIstream &_optional) noexcept
        :optional(&_optional) {}

    /**
     * Allows the #Istream to resume, but does not trigger reading.
     */
    void Resume() noexcept;

    /**
     * Discard the stream contents.
     */
    void Discard() noexcept;
};

/**
 * An istream facade which holds an optional istream.  It blocks until
 * it is told to resume or to discard the inner istream.  Errors are
 * reported to the handler immediately.
 */
std::pair<UnusedIstreamPtr, SharedPoolPtr<OptionalIstreamControl>>
istream_optional_new(struct pool &pool, UnusedIstreamPtr input) noexcept;
