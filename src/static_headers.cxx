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

#include "static_headers.hxx"
#include "strmap.hxx"
#include "pool/pool.hxx"
#include "http/Date.hxx"
#include "io/FileDescriptor.hxx"
#include "util/HexFormat.h"

#include <attr/xattr.h>

#include <assert.h>
#include <sys/stat.h>
#include <sys/types.h>

static bool
ReadETag(FileDescriptor fd, char *buffer, size_t size) noexcept
{
    assert(fd.IsDefined());
    assert(size > 4);

    const auto nbytes = fgetxattr(fd.Get(), "user.ETag", buffer + 1, size - 3);
    if (nbytes <= 0)
        return false;

    assert((size_t)nbytes < size);

    buffer[0] = '"';
    buffer[nbytes + 1] = '"';
    buffer[nbytes + 2] = 0;
    return true;
}

static void
static_etag(char *p, const struct stat &st)
{
    *p++ = '"';

    p += format_uint32_hex(p, (uint32_t)st.st_dev);

    *p++ = '-';

    p += format_uint32_hex(p, (uint32_t)st.st_ino);

    *p++ = '-';

    p += format_uint32_hex(p, (uint32_t)st.st_mtime);

    *p++ = '"';
    *p = 0;
}

void
GetAnyETag(char *buffer, size_t size,
           FileDescriptor fd, const struct stat &st) noexcept
{
    if (!fd.IsDefined() || !ReadETag(fd, buffer, size))
        static_etag(buffer, st);
}

bool
load_xattr_content_type(char *buffer, size_t size, FileDescriptor fd) noexcept
{
    if (!fd.IsDefined())
        return false;

    ssize_t nbytes = fgetxattr(fd.Get(), "user.Content-Type",
                               buffer, size - 1);
    if (nbytes <= 0)
        return false;

    assert((size_t)nbytes < size);
    buffer[nbytes] = 0;
    return true;
}

StringMap
static_response_headers(struct pool &pool,
                        FileDescriptor fd, const struct stat &st,
                        const char *content_type)
{
    StringMap headers;

    if (S_ISCHR(st.st_mode))
        return headers;

    char buffer[256];

    if (content_type == nullptr)
        content_type = load_xattr_content_type(buffer, sizeof(buffer), fd)
            ? p_strdup(&pool, buffer)
            : "application/octet-stream";

    headers.Add(pool, "content-type", content_type);

    headers.Add(pool, "last-modified",
                p_strdup(&pool, http_date_format(std::chrono::system_clock::from_time_t(st.st_mtime))));

    GetAnyETag(buffer, sizeof(buffer), fd, st);
    headers.Add(pool, "etag", p_strdup(&pool, buffer));

    return headers;
}
