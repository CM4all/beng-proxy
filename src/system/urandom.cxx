/*
 * Copyright 2007-2019 Content Management AG
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

#include "urandom.hxx"
#include "system/Error.hxx"
#include "io/Open.hxx"
#include "io/UniqueFileDescriptor.hxx"

static size_t
Read(const char *path, FileDescriptor fd, void *p, size_t size)
{
    ssize_t nbytes = fd.Read(p, size);
    if (nbytes < 0)
        throw FormatErrno("Failed to read from %s", path);

    if (nbytes == 0)
        throw std::runtime_error(std::string("Short read from ") + path);

    return nbytes;
}

static void
FullRead(const char *path, FileDescriptor fd, void *_p, size_t size)
{
    uint8_t *p = (uint8_t *)_p;

    while (size > 0) {
        size_t nbytes = Read(path, fd, p, size);
        size -= nbytes;
    }
}

static size_t
Read(const char *path, void *p, size_t size)
{
    return Read(path, OpenReadOnly(path), p, size);
}

static void
FullRead(const char *path, void *p, size_t size)
{
    FullRead(path, OpenReadOnly(path), p, size);
}

size_t
UrandomRead(void *p, size_t size)
{
    return Read("/dev/urandom", p, size);
}

void
UrandomFill(void *p, size_t size)
{
    FullRead("/dev/urandom", p, size);
}
