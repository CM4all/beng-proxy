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

#include "istream_optional.hxx"
#include "ForwardIstream.hxx"
#include "UnusedPtr.hxx"
#include "istream_null.hxx"

#include <assert.h>

class OptionalIstream final : public ForwardIstream {
    bool resumed = false;

public:
    OptionalIstream(struct pool &p, Istream &_input)
        :ForwardIstream(p, _input) {}

    void Resume() {
        resumed = true;
    }

    void Discard() {
        assert(!resumed);

        resumed = true;

        /* replace the input with a "null" istream */
        ReplaceInputDirect(istream_null_new(GetPool()));
    }

    /* virtual methods from class Istream */

    off_t _GetAvailable(bool partial) override {
        /* can't respond to this until we're resumed, because the
           original input can be discarded */
        return resumed ? ForwardIstream::_GetAvailable(partial) : -1;
    }

    void _Read() override {
        if (resumed)
            ForwardIstream::_Read();
    }

    int _AsFd() override {
        return resumed
            ? ForwardIstream::_AsFd()
            : -1;
    }

    /* handler */

    size_t OnData(const void *data, size_t length) override {
        return resumed ? InvokeData(data, length) : 0;
    }
};

Istream *
istream_optional_new(struct pool &pool, Istream &input)
{
    return NewIstream<OptionalIstream>(pool, input);
}

void
istream_optional_resume(Istream &istream)
{
    auto &optional = (OptionalIstream &)istream;
    optional.Resume();
}

void
istream_optional_discard(Istream &istream)
{
    auto &optional = (OptionalIstream &)istream;
    optional.Discard();
}
