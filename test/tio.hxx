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
 * I/O utilities for unit tests.
 */

#include "util/ByteOrder.hxx"

#include "util/Compiler.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>

static void
read_full(void *_p, size_t length)
{
    uint8_t *p = (uint8_t *)_p, *const end = p + length;

    while (p < end) {
        ssize_t nbytes = recv(0, p, length, MSG_WAITALL);
        if (nbytes <= 0)
            _exit(EXIT_FAILURE);
        p += nbytes;
    }
}

gcc_unused
static uint8_t
read_byte(size_t *remaining_r)
{
    uint8_t value;

    if (*remaining_r < sizeof(value))
        _exit(EXIT_FAILURE);

    read_full(&value, sizeof(value));
    (*remaining_r) -= sizeof(value);
    return value;
}

gcc_unused
static uint16_t
read_short(size_t *remaining_r)
{
    uint16_t value;

    if (*remaining_r < sizeof(value))
        _exit(EXIT_FAILURE);

    read_full(&value, sizeof(value));
    (*remaining_r) -= sizeof(value);
    return FromBE16(value);
}

gcc_unused
static void
discard(size_t length)
{
    while (length > 0) {
        uint8_t buffer[1024];
        size_t nbytes = length;
        if (nbytes > sizeof(buffer))
            nbytes = sizeof(buffer);
        read_full(buffer, nbytes);
        length -= nbytes;
    }
}

static void
write_full(const void *_p, size_t length)
{
    const uint8_t *p = (const uint8_t *)_p, *const end = p + length;

    while (p < end) {
        ssize_t nbytes = send(0, p, length, MSG_NOSIGNAL);
        if (nbytes <= 0)
            _exit(EXIT_FAILURE);
        p += nbytes;
    }
}

gcc_unused
static void
write_byte(const uint8_t value)
{
    write_full(&value, sizeof(value));
}

gcc_unused
static void
write_short(uint16_t value)
{
    const uint16_t buffer = ToBE16(value);
    write_full(&buffer, sizeof(buffer));
}

gcc_unused
static void
fill(size_t length)
{
    while (length > 0) {
        static uint8_t buffer[1024];
        size_t nbytes = length;
        if (nbytes > sizeof(buffer))
            nbytes = sizeof(buffer);
        write_full(buffer, nbytes);
        length -= nbytes;
    }
}
