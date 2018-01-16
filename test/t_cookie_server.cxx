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

#include "cookie_server.hxx"
#include "header_writer.hxx"
#include "strmap.hxx"
#include "pool/RootPool.hxx"

#include <assert.h>
#include <string.h>
#include <stdio.h>

int main(int argc gcc_unused, char **argv gcc_unused) {
    RootPool pool;

    {
        const auto cookies = cookie_map_parse(pool, "a=b");
        assert(strcmp(cookies.Get("a"), "b") == 0);
    }

    {
        const auto cookies = cookie_map_parse(pool, "c=d;e=f");
        assert(strcmp(cookies.Get("c"), "d") == 0);
        assert(strcmp(cookies.Get("e"), "f") == 0);
    }

    {
        const auto cookies = cookie_map_parse(pool, "quoted=\"quoted!\\\\");
        assert(strcmp(cookies.Get("quoted"), "quoted!\\") == 0);
    }

    {
        const auto cookies = cookie_map_parse(pool, "invalid1=foo\t");
        assert(strcmp(cookies.Get("invalid1"), "foo") == 0);
    }

    {
        /* this is actually invalid, but unfortunately RFC ignorance
           is viral, and forces us to accept square brackets :-( */
        const auto cookies = cookie_map_parse(pool, "invalid2=foo |[bar] ,");
        assert(strcmp(cookies.Get("invalid2"), "foo |[bar] ,") == 0);
    }

    assert(strcmp(cookie_exclude("foo=\"bar\"", "abc", pool),
                  "foo=\"bar\"") == 0);

    assert(cookie_exclude("foo=\"bar\"", "foo", pool) == nullptr);

    assert(strcmp(cookie_exclude("a=\"b\"", "foo", pool),
                  "a=\"b\"") == 0);

    assert(strcmp(cookie_exclude("a=b", "foo", pool),
                  "a=b") == 0);

    assert(strcmp(cookie_exclude("a=\"b\"; foo=\"bar\"; c=\"d\"", "foo", pool),
                  "a=\"b\"; c=\"d\"") == 0);

    assert(strcmp(cookie_exclude("foo=\"bar\"; c=\"d\"", "foo", pool),
                  "c=\"d\"") == 0);

    assert(strcmp(cookie_exclude("a=\"b\"; foo=\"bar\"", "foo", pool),
                  "a=\"b\"; ") == 0);

    assert(strcmp(cookie_exclude("foo=\"duplicate\"; a=\"b\"; foo=\"bar\"; c=\"d\"", "foo", pool),
                  "a=\"b\"; c=\"d\"") == 0);
}
