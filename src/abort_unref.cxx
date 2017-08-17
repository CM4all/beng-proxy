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

#include "abort_unref.hxx"
#include "pool.hxx"
#include "util/Cast.hxx"
#include "util/Cancellable.hxx"

struct UnrefOnAbort final : Cancellable {
    struct pool &pool;
    CancellablePointer cancel_ptr;

#ifdef TRACE
    const char *const file;
    unsigned line;
#endif

    UnrefOnAbort(struct pool &_pool,
                 CancellablePointer &_cancel_ptr
                 TRACE_ARGS_DECL_)
        :pool(_pool)
         TRACE_ARGS_INIT {
        _cancel_ptr = *this;
    }

    /* virtual methods from class Cancellable */
    void Cancel() override {
        cancel_ptr.Cancel();
        pool_unref_fwd(&pool);
    }
};

/*
 * constructor
 *
 */

CancellablePointer &
async_unref_on_abort_impl(struct pool &pool,
                          CancellablePointer &cancel_ptr
                          TRACE_ARGS_DECL)
{
    auto uoa = NewFromPool<UnrefOnAbort>(pool, pool, cancel_ptr
                                         TRACE_ARGS_FWD);
    return uoa->cancel_ptr;
}
