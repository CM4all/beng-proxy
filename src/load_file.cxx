/*
 * Copyright 2007-2021 CM4all GmbH
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

#include "load_file.hxx"
#include "HttpMessageResponse.hxx"
#include "pool/pool.hxx"
#include "io/Open.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "system/Error.hxx"
#include "http/Status.h"
#include "util/ConstBuffer.hxx"
#include "util/StringFormat.hxx"

ConstBuffer<void>
LoadFile(struct pool &pool, const char *path, off_t max_size)
{
    auto fd = OpenReadOnly(path);

    off_t size = fd.GetSize();
    if (size < 0)
        throw FormatErrno("Failed to stat %s", path);

    if (size > max_size)
        throw HttpMessageResponse(HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                  StringFormat<256>("File is too large: %s", path));

    if (size == 0)
        return { "", 0 };

    void *p = p_malloc(&pool, size);
    if (p == nullptr)
        throw std::bad_alloc();

    ssize_t nbytes = fd.Read(p, size);
    if (nbytes < 0)
        throw FormatErrno("Failed to read from %s", path);

    if (size_t(nbytes) != size_t(size))
        throw HttpMessageResponse(HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                  StringFormat<256>("Short read from: %s", path));

    return { p, size_t(size) };
}
