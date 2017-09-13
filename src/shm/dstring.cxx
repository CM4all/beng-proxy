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
 * String allocation for distributed pools.
 */

#include "dpool.hxx"
#include "util/StringView.hxx"

#include <assert.h>
#include <string.h>

char *
d_memdup(struct dpool &pool, const void *src, size_t length)
{
    void *dest = d_malloc(pool, length);
    memcpy(dest, src, length);
    return (char *)dest;
}

char *
d_strdup(struct dpool &pool, const char *src)
{
    return (char *)d_memdup(pool, src, strlen(src) + 1);
}

char *
d_strdup(struct dpool &pool, StringView src)
{
    return d_strndup(pool, src.data, src.size);
}

char *
d_strndup(struct dpool &pool, const char *src, size_t length)
{
    char *dest = (char *)d_malloc(pool, length + 1);
    memcpy(dest, src, length);
    dest[length] = 0;
    return dest;
}

StringView
DupStringView(struct dpool &pool, StringView src)
    throw(std::bad_alloc)
{
    if (src.IsNull())
        return nullptr;

    if (src.IsEmpty())
        return "";

    const char *data = d_memdup(pool, src.data, src.size);
    return {data, src.size};
}

void
FreeStringView(struct dpool &pool, StringView s)
{
    if (!s.IsEmpty())
        d_free(pool, s.data);
}
