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

#include "memcached/Protocol.hxx"
#include "fb_pool.hxx"
#include "util/ByteOrder.hxx"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

static void
read_full_or_abort(int fd, void *dest0, size_t length)
{
    char *dest = (char *)dest0;
    ssize_t nbytes;

    while (length > 0) {
        nbytes = read(fd, dest, length);
        if (nbytes < 0) {
            perror("read() failed");
            exit(2);
        }

        if (nbytes == 0)
            exit(0);

        dest += nbytes;
        length -= nbytes;
    }
}

static void
read_discard(int fd, size_t length)
{
    static char buffer[4096];

    while (length >= sizeof(buffer)) {
        read_full_or_abort(fd, buffer, sizeof(buffer));
        length -= sizeof(buffer);
    }

    read_full_or_abort(fd, buffer, length);
}

static void
write_full_or_abort(int fd, const void *dest0, size_t length)
{
    const char *dest = (const char *)dest0;
    ssize_t nbytes;

    while (length > 0) {
        nbytes = write(fd, dest, length);
        if (nbytes < 0) {
            perror("write() failed");
            exit(2);
        }

        if (nbytes == 0) {
            fprintf(stderr, "empty write()\n");
            exit(2);
        }

        dest += nbytes;
        length -= nbytes;
    }
}

int main(int argc, char **argv)
{
    struct memcached_request_header request_header;
    static constexpr char response_key[3] = {'f','o','o'};
    static char response_body1[1024];
    static char response_body2[2 * FB_SIZE];
    const struct memcached_response_header response_header = {
        .magic = MEMCACHED_MAGIC_RESPONSE,
        .opcode = 0,
        .key_length = ToBE16(sizeof(response_key)),
        .extras_length = 0,
        .data_type = 0,
        .status = MEMCACHED_STATUS_NO_ERROR,
        .body_length = ToBE32(sizeof(response_key) +
                              sizeof(response_body1) +
                              sizeof(response_body2)),
        .message_id = 0,
        .cas = {0, 0, 0, 0, 0, 0, 0, 0},
    };

    (void)argc;
    (void)argv;

    while (true) {
        read_full_or_abort(0, &request_header, sizeof(request_header));

        if (request_header.magic != MEMCACHED_MAGIC_REQUEST) {
            fprintf(stderr, "wrong magic: 0x%02x\n", request_header.magic);
            return 2;
        }

        read_discard(0, FromBE32(request_header.body_length));

        write_full_or_abort(1, &response_header, sizeof(response_header));
        write_full_or_abort(1, response_key, sizeof(response_key));
        write_full_or_abort(1, response_body1, sizeof(response_body1));
        write_full_or_abort(1, response_body2, sizeof(response_body2));
    }
}
