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

#include "header_copy.hxx"
#include "strmap.hxx"
#include "util/StringCompare.hxx"

#include <assert.h>
#include <string.h>

void
header_copy_one(const StringMap &in, StringMap &out, const char *key)
{
    assert(key != nullptr);

    const auto r = in.EqualRange(key);
    for (auto i = r.first; i != r.second; ++i)
        out.Add(key, i->value);
}

void
header_copy_list(const StringMap &in, StringMap &out,
                 const char *const*keys)
{
    assert(keys != nullptr);

    for (; *keys != nullptr; ++keys)
        header_copy_one(in, out, *keys);
}

void
header_copy_prefix(const StringMap &in, StringMap &out,
                   const char *_prefix)
{
    assert(_prefix != nullptr);
    assert(*_prefix != 0);

    const StringView prefix(_prefix);

    for (const auto &i : in)
        if (StringStartsWith(i.key, prefix))
            out.Add(i.key, i.value);
}
