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

#include "cookie_client.hxx"
#include "cookie_jar.hxx"
#include "header_writer.hxx"
#include "RootPool.hxx"
#include "shm/shm.hxx"
#include "shm/dpool.hxx"
#include "strmap.hxx"

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

static void
Test1(struct dpool *dpool)
{
    RootPool pool;
    StringMap headers(pool);

    CookieJar jar(*dpool);

    /* empty cookie jar */
    cookie_jar_http_header(jar, "foo.bar", "/", headers, pool);
    assert(headers.Get("cookie") == nullptr);
    assert(headers.Get("cookie2") == nullptr);

    /* wrong domain */
    cookie_jar_set_cookie2(jar, "a=b", "other.domain", nullptr);
    cookie_jar_http_header(jar, "foo.bar", "/", headers, pool);
    assert(headers.Get("cookie") == nullptr);
    assert(headers.Get("cookie2") == nullptr);

    /* correct domain */
    cookie_jar_set_cookie2(jar, "a=b", "foo.bar", nullptr);
    cookie_jar_http_header(jar, "foo.bar", "/", headers, pool);
    assert(strcmp(headers.Get("cookie"), "a=b") == 0);

    /* another cookie */
    headers.Clear();
    cookie_jar_set_cookie2(jar, "c=d", "foo.bar", nullptr);
    cookie_jar_http_header(jar, "foo.bar", "/", headers, pool);
    assert(strcmp(headers.Get("cookie"), "c=d; a=b") == 0);

    /* delete a cookie */
    headers.Clear();
    cookie_jar_set_cookie2(jar, "c=xyz;max-age=0", "foo.bar", nullptr);
    cookie_jar_http_header(jar, "foo.bar", "/", headers, pool);
    assert(strcmp(headers.Get("cookie"), "a=b") == 0);

    /* other domain */
    headers.Clear();
    cookie_jar_http_header(jar, "other.domain", "/some_path", headers, pool);
    assert(strcmp(headers.Get("cookie"), "a=b") == 0);
}

static void
Test2(struct dpool *dpool)
{
    RootPool pool;
    StringMap headers(pool);

    /* wrong path */
    CookieJar jar(*dpool);

    cookie_jar_set_cookie2(jar, "a=b;path=\"/foo\"", "foo.bar", "/bar/x");
    cookie_jar_http_header(jar, "foo.bar", "/", headers, pool);
    assert(headers.Get("cookie") == nullptr);
    assert(headers.Get("cookie2") == nullptr);

    /* correct path */
    headers.Clear();
    cookie_jar_set_cookie2(jar, "a=b;path=\"/bar\"", "foo.bar", "/bar/x");
    cookie_jar_http_header(jar, "foo.bar", "/bar", headers, pool);
    assert(strcmp(headers.Get("cookie"), "a=b") == 0);

    /* delete: path mismatch */
    headers.Clear();
    cookie_jar_set_cookie2(jar, "a=b;path=\"/foo\";max-age=0",
                           "foo.bar", "/foo/x");
    cookie_jar_http_header(jar, "foo.bar", "/bar", headers, pool);
    assert(strcmp(headers.Get("cookie"), "a=b") == 0);

    /* delete: path match */
    headers.Clear();
    cookie_jar_set_cookie2(jar, "a=b;path=\"/bar\";max-age=0",
                           "foo.bar", "/bar/x");
    cookie_jar_http_header(jar, "foo.bar", "/bar", headers, pool);
    assert(headers.Get("cookie") == nullptr);
    assert(headers.Get("cookie2") == nullptr);
}

int main(int argc gcc_unused, char **argv gcc_unused) {
    struct shm *shm;
    struct dpool *dpool;

    shm = shm_new(1024, 512);
    dpool = dpool_new(*shm);

    Test1(dpool);
    Test2(dpool);

    dpool_destroy(dpool);
    shm_close(shm);
}
