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

#include "http/CookieClient.hxx"
#include "http/CookieJar.hxx"
#include "http/HeaderWriter.hxx"
#include "pool/RootPool.hxx"
#include "memory/fb_pool.hxx"
#include "strmap.hxx"
#include "memory/GrowingBuffer.hxx"
#include "util/ConstBuffer.hxx"
#include "AllocatorPtr.hxx"

#include <stdio.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char **argv) {
    const ScopeFbPoolInit fb_pool_init;
    RootPool pool;
    const AllocatorPtr alloc(pool);

    CookieJar jar;

    for (int i = 1; i < argc; ++i)
        cookie_jar_set_cookie2(jar, argv[i], "foo.bar", nullptr);

    StringMap headers;
    cookie_jar_http_header(jar, "foo.bar", "/x", headers, alloc);

    GrowingBufferReader reader(headers_dup(headers));

    ConstBuffer<void> src;
    while (!(src = reader.Read()).IsNull()) {
        ssize_t nbytes = write(1, src.data, src.size);
        if (nbytes < 0) {
            perror("write() failed");
            return 1;
        }

        if (nbytes == 0)
            break;

        reader.Consume((size_t)nbytes);
    }
}
