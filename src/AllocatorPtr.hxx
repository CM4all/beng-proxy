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

#ifndef BENG_PROXY_ALLOCATOR_PTR_HXX
#define BENG_PROXY_ALLOCATOR_PTR_HXX

#include "pool/pool.hxx"
#include "util/StringView.hxx"

#include <string.h>

struct StringView;
template<typename T> struct ConstBuffer;

class AllocatorPtr {
    struct pool &pool;

public:
    AllocatorPtr(struct pool &_pool):pool(_pool) {}

    char *Dup(const char *src) {
        return p_strdup(&pool, src);
    }

    const char *CheckDup(const char *src) {
        return p_strdup_checked(&pool, src);
    }

    template<typename... Args>
    char *Concat(Args&&... args) {
        const size_t length = ConcatLength(args...);
        char *result = NewArray<char>(length + 1);
        *ConcatCopy(result, args...) = 0;
        return result;
    }

    template<typename T, typename... Args>
    T *New(Args&&... args) {
        return NewFromPool<T>(pool, std::forward<Args>(args)...);
    }

    template<typename T>
    T *NewArray(size_t n) {
        return PoolAlloc<T>(pool, n);
    }

    void *Dup(const void *data, size_t size) {
        return p_memdup(&pool, data, size);
    }

    ConstBuffer<void> Dup(ConstBuffer<void> src);

    template<typename T>
    ConstBuffer<T> Dup(ConstBuffer<T> src) {
        return ConstBuffer<T>::FromVoid(Dup(src.ToVoid()));
    }

    StringView Dup(StringView src);
    const char *DupZ(StringView src);

    const char *DupToLower(StringView src) {
        return p_strdup_lower(pool, src);
    }

private:
    template<typename... Args>
    static size_t ConcatLength(const char *s, Args... args) {
        return strlen(s) + ConcatLength(args...);
    }

    template<typename... Args>
    static constexpr size_t ConcatLength(StringView s, Args... args) {
        return s.size + ConcatLength(args...);
    }

    static constexpr size_t ConcatLength() {
        return 0;
    }

    template<typename... Args>
    static char *ConcatCopy(char *p, const char *s, Args... args) {
        return ConcatCopy(stpcpy(p, s), args...);
    }

    template<typename... Args>
    static char *ConcatCopy(char *p, StringView s, Args... args) {
        return ConcatCopy((char *)mempcpy(p, s.data, s.size), args...);
    }

    template<typename... Args>
    static char *ConcatCopy(char *p) {
        return p;
    }
};

#endif
