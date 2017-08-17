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

#include "fb_pool.hxx"
#include "SlicePool.hxx"

#include <assert.h>

/* TODO: this increase from 8 kB to 16 kB is only here to work around
 * a OpenSSL problem; TLS packets may be up to 16 kB, and our
 * FifoBufferBio can't handle the resulting SSL_ERROR_WANT_READ when
 * the buffer is already full; we need a better general solution for
 * this */
/* TODO: after restoring this buffer to 8 kB, shrink the stack buffer
 * in ThreadSocketFilter::SubmitDecryptedInput() as well */
static constexpr size_t FB_SIZE = 2 * 8192;
//static constexpr size_t FB_SIZE = 8192;

class SliceFifoBufferPool {
    SlicePool *const pool;

public:
    SliceFifoBufferPool()
        :pool(slice_pool_new(FB_SIZE, 256)) {
        assert(pool != nullptr);
    }

    ~SliceFifoBufferPool() {
        slice_pool_free(pool);
    }

    SlicePool &Get() {
        return *pool;
    }

    void ForkCow(bool inherit) {
        slice_pool_fork_cow(*pool, inherit);
    }

    void Compress() {
        slice_pool_compress(pool);
    }
};

static SliceFifoBufferPool *fb_pool;

void
fb_pool_init()
{
    assert(fb_pool == nullptr);

    fb_pool = new SliceFifoBufferPool();
}

void
fb_pool_deinit(void)
{
    assert(fb_pool != nullptr);

    delete fb_pool;
    fb_pool = nullptr;
}

void
fb_pool_fork_cow(bool inherit)
{
    assert(fb_pool != nullptr);

    fb_pool->ForkCow(inherit);
}

SlicePool &
fb_pool_get()
{
    return fb_pool->Get();
}

void
fb_pool_compress(void)
{
    assert(fb_pool != nullptr);

    fb_pool->Compress();
}
