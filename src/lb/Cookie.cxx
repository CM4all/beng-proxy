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

#include "Cookie.hxx"
#include "strmap.hxx"
#include "pool/tpool.hxx"
#include "cookie_server.hxx"
#include "pool/pool.hxx"

#include <assert.h>
#include <string.h>
#include <stdlib.h>

sticky_hash_t
lb_cookie_get(const StringMap &request_headers)
{
    const AutoRewindPool auto_rewind(*tpool);

    const char *cookie = request_headers.Get("cookie");
    if (cookie == NULL)
        return 0;

    const auto jar = cookie_map_parse(*tpool, cookie);

    const char *p = jar.Get("beng_lb_node");
    if (p == NULL || memcmp(p, "0-", 2) != 0)
        return 0;

    p += 2;

    char *endptr;
    unsigned long id = strtoul(p, &endptr, 16);
    if (endptr == p || *endptr != 0)
        return 0;

    return (sticky_hash_t)id;
}

sticky_hash_t
lb_cookie_generate(unsigned n)
{
    assert(n >= 2);

    return (random() % n) + 1;
}
