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

#ifndef BENG_EXPANSIBLE_BUFFER_HXX
#define BENG_EXPANSIBLE_BUFFER_HXX

#include "util/Compiler.h"

#include <stddef.h>

struct pool;
template<typename T> struct ConstBuffer;
struct StringView;

/**
 * A buffer which grows automatically.  Compared to growing_buffer, it
 * is optimized to be read as one complete buffer, instead of many
 * smaller chunks.  Additionally, it can be reused.
 */
class ExpansibleBuffer {
    struct pool &pool;
    char *buffer;
    const size_t hard_limit;
    size_t max_size;
    size_t size = 0;

public:
    /**
     * @param _hard_limit the buffer will refuse to grow beyond this size
     */
    ExpansibleBuffer(struct pool &_pool,
                     size_t initial_size, size_t _hard_limit);

    ExpansibleBuffer(const ExpansibleBuffer &) = delete;
    ExpansibleBuffer &operator=(const ExpansibleBuffer &) = delete;

    bool IsEmpty() const {
        return size == 0;
    }

    size_t GetSize() const {
        return size;
    }

    void Clear();

    /**
     * @return nullptr if the operation would exceed the hard limit
     */
    void *Write(size_t length);

    /**
     * @return false if the operation would exceed the hard limit
     */
    bool Write(const void *p, size_t length);

    /**
     * @return false if the operation would exceed the hard limit
     */
    bool Write(const char *p);

    /**
     * @return false if the operation would exceed the hard limit
     */
    bool Set(const void *p, size_t new_size);

    bool Set(StringView p);

    gcc_pure
    ConstBuffer<void> Read() const;

    gcc_pure
    const char *ReadString();

    gcc_pure
    StringView ReadStringView() const;

    void *Dup(struct pool &_pool) const;

    char *StringDup(struct pool &_pool) const;

private:
    bool Resize(size_t new_max_size);
};

#endif
