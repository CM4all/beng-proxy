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

#ifndef BENG_PROXY_ISTREAM_SUBST_HXX
#define BENG_PROXY_ISTREAM_SUBST_HXX

#include "util/Compiler.h"

#include <algorithm>

#include <stddef.h>

struct pool;
class Istream;
class UnusedIstreamPtr;
struct SubstNode;

class SubstTree {
    SubstNode *root = nullptr;

public:
    SubstTree() = default;

    SubstTree(SubstTree &&src) noexcept
        :root(std::exchange(src.root, nullptr)) {}

    SubstTree &operator=(SubstTree &&src) noexcept {
        using std::swap;
        swap(root, src.root);
        return *this;
    }

    bool Add(struct pool &pool, const char *a0, const char *b, size_t b_length) noexcept;
    bool Add(struct pool &pool, const char *a0, const char *b) noexcept;

    gcc_pure
    std::pair<const SubstNode *, const char *> FindFirstChar(const char *data,
                                                             size_t length) noexcept;
};

/**
 * This istream filter substitutes a word with another string.
 */
Istream *
istream_subst_new(struct pool *pool, UnusedIstreamPtr input,
                  SubstTree tree);

#endif
