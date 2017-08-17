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

#include "abort_close.hxx"
#include "pool.hxx"
#include "istream/istream.hxx"
#include "util/Cast.hxx"
#include "util/Cancellable.hxx"

struct CloseOnAbort final : Cancellable {
    Istream &istream;
    CancellablePointer cancel_ptr;

    CloseOnAbort(Istream &_istream,
                 CancellablePointer &_cancel_ptr)
        :istream(_istream) {
        _cancel_ptr = *this;
    }

    /* virtual methods from class Cancellable */
    void Cancel() override {
        cancel_ptr.Cancel();
        istream.CloseUnused();
    }
};

/*
 * constructor
 *
 */

CancellablePointer &
async_close_on_abort(struct pool &pool, Istream &istream,
                     CancellablePointer &cancel_ptr)
{
    assert(!istream.HasHandler());

    auto coa = NewFromPool<struct CloseOnAbort>(pool, istream, cancel_ptr);
    return coa->cancel_ptr;
}

CancellablePointer &
async_optional_close_on_abort(struct pool &pool, Istream *istream,
                              CancellablePointer &cancel_ptr)
{
    return istream != nullptr
        ? async_close_on_abort(pool, *istream, cancel_ptr)
        : cancel_ptr;
}
