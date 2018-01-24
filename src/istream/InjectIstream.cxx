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

#include "InjectIstream.hxx"
#include "ForwardIstream.hxx"

class InjectIstream final : public ForwardIstream {
public:
    InjectIstream(struct pool &p, Istream &_input)
        :ForwardIstream(p, _input) {}

    void InjectFault(std::exception_ptr ep) {
        if (HasInput())
            input.Close();

        DestroyError(ep);
    }

    /* virtual methods from class Istream */

    off_t _GetAvailable(bool partial) override {
        /* never return the total length, because the caller may then
           make assumptions on when this stream ends */
        return partial && HasInput()
            ? ForwardIstream::_GetAvailable(partial)
            : -1;
    }

    void _Read() override {
        if (HasInput())
            ForwardIstream::_Read();
    }

    int _AsFd() override {
        return -1;
    }

    /* virtual methods from class IstreamHandler */

    void OnEof() noexcept override {
        ClearInput();
    }

    void OnError(std::exception_ptr) noexcept override {
        ClearInput();
    }
};

/*
 * constructor
 *
 */

Istream *
istream_inject_new(struct pool &pool, Istream &input)
{
    return NewIstream<InjectIstream>(pool, input);
}

void
istream_inject_fault(Istream &i_inject, std::exception_ptr ep)
{
    auto &inject = (InjectIstream &)i_inject;
    inject.InjectFault(ep);
}
