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

#include "relocate_uri.hxx"
#include "http_address.hxx"
#include "pool/RootPool.hxx"
#include "util/StringView.hxx"

#include <stdio.h>

static void
CheckString(const char *expected, const char *value)
{
    if (expected == nullptr) {
        if (value != nullptr) {
            fprintf(stderr, "Expected nullptr, got '%s'\n", value);
            abort();
        }
    } else {
        if (value == nullptr) {
            fprintf(stderr, "Expected '%s', got nullptr\n", expected);
            abort();
        } else if (strcmp(expected, value) != 0) {
            fprintf(stderr, "Expected '%s', got '%s'\n", expected, value);
            abort();
        }
    }
}

struct RelocateUriTest {
    const char *uri;
    const char *internal_host;
    const char *internal_path;
    const char *external_path;
    const char *base;
    const char *expected;
};

static constexpr RelocateUriTest relocate_uri_tests[] = {
    { "http://internal-host/int-base/c", "internal-host", "/int-base/request",
      "/ext-base/request", "/ext-base/",
      "https://external-host:80/ext-base/c" },

    { "//internal-host/int-base/c", "internal-host", "/int-base/request",
      "/ext-base/request", "/ext-base/",
      "https://external-host:80/ext-base/c" },

    { "/int-base/c", "i", "/int-base/request",
      "/ext-base/request", "/ext-base/",
      "https://external-host:80/ext-base/c" },

    /* fail: relative URI */
    { "c", "internal-host", "/int-base/request",
      "/ext-base/request", "/ext-base/",
      nullptr },

    /* fail: host mismatch */
    { "//host-mismatch/int-base/c", "internal-host", "/int-base/request",
      "/ext-base/request", "/ext-base/",
      nullptr },

    /* fail: internal base mismatch */
    { "http://internal-host/wrong-base/c", "internal-host", "/int-base/request",
      "/ext-base/request", "/ext-base/",
      nullptr },

    /* fail: external base mismatch */
    { "http://internal-host/int-base/c", "internal-host", "/int-base/request",
      "/wrong-base/request", "/ext-base/",
      nullptr },
};

static void
CheckRelocateUri(struct pool &pool, const char *uri,
                 const char *internal_host, StringView internal_path,
                 const char *external_scheme, const char *external_host,
                 StringView external_path, StringView base,
                 const char *expected)
{
    auto *relocated = RelocateUri(pool, uri, internal_host, internal_path,
                                  external_scheme, external_host,
                                  external_path, base);
    CheckString(expected, relocated);
}

static void
TestRelocateUri(struct pool &pool)
{
    for (const auto &i : relocate_uri_tests)
        CheckRelocateUri(pool, i.uri, i.internal_host, i.internal_path,
                         "https", "external-host:80",
                         i.external_path, i.base,
                         i.expected);
}

/*
 * the main test code
 *
 */

int main(gcc_unused int argc, gcc_unused char **argv)
{
    TestRelocateUri(RootPool());
    return EXIT_SUCCESS;
}
