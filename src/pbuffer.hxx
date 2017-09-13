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

/*
 * Allocating struct ConstBuffer from memory pool.
 */

#ifndef PBUFFER_HXX
#define PBUFFER_HXX

#include "pool.hxx"
#include "util/ConstBuffer.hxx"
#include "util/StringView.hxx"

template<typename T>
static inline ConstBuffer<T>
DupBuffer(pool &p, ConstBuffer<T> src)
{
    if (src.IsNull())
        return ConstBuffer<T>::Null();

    if (src.IsEmpty())
        return ConstBuffer<T>::FromVoid({"", 0});

    ConstBuffer<void> src_v = src.ToVoid();
    ConstBuffer<void> dest_v(p_memdup(&p, src_v.data, src_v.size), src_v.size);
    return ConstBuffer<T>::FromVoid(dest_v);
}

static inline StringView
DupBuffer(pool &p, StringView src)
{
    if (src.IsNull())
        return nullptr;

    if (src.IsEmpty())
        return "";

    return StringView((const char *)p_memdup(&p, src.data, src.size),
                      src.size);
}

/**
 * Allocate a new buffer with data concatenated from the given source
 * buffers.  If one is empty, this may return a pointer to the other
 * buffer.
 */
ConstBuffer<void>
LazyCatBuffer(struct pool &pool, ConstBuffer<void> a, ConstBuffer<void> b);

#endif
