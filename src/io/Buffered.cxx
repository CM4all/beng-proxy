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

#include "Buffered.hxx"
#include "util/ForeignFifoBuffer.hxx"

#include <assert.h>
#include <unistd.h>
#include <errno.h>

ssize_t
read_to_buffer(int fd, ForeignFifoBuffer<uint8_t> &buffer, size_t length)
{
    assert(fd >= 0);

    auto w = buffer.Write();
    if (w.empty())
        return -2;

    if (length > w.size)
        length = w.size;

    ssize_t nbytes = read(fd, w.data, length);
    if (nbytes > 0)
        buffer.Append((size_t)nbytes);

    return nbytes;
}

ssize_t
write_from_buffer(int fd, ForeignFifoBuffer<uint8_t> &buffer)
{
    auto r = buffer.Read();
    if (r.empty())
        return -2;

    ssize_t nbytes = write(fd, r.data, r.size);
    if (nbytes >= 0)
        buffer.Consume((size_t)nbytes);
    else if (errno == EAGAIN)
        nbytes = 0;

    return nbytes;
}
