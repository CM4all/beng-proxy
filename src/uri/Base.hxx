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

/*
 * Functions for working with base URIs.
 */

#ifndef BENG_PROXY_URI_BASE_HXX
#define BENG_PROXY_URI_BASE_HXX

#include <stddef.h>

struct StringView;

/**
 * Calculate the URI tail after a base URI from a request URI.
 * Returns nullptr if no such tail URI is possible (e.g. if the
 * specified URI is not "within" the base, or if there is no base at
 * all).
 *
 * @param uri the URI specified by the tcache client, may be nullptr
 * @param base the base URI, as given by the translation server,
 * stored in the cache item
 */
[[gnu::pure]]
const char *
base_tail(const char *uri, StringView base) noexcept;

/**
 * Similar to base_tail(), but assert that there is a base match.
 */
[[gnu::pure]]
const char *
require_base_tail(const char *uri, StringView base) noexcept;

/**
 * Determine the length of the base prefix in the given string.
 *
 * @return (size_t)-1 on mismatch
 */
[[gnu::pure]]
size_t
base_string(StringView uri, StringView tail) noexcept;

/**
 * Is the given string a valid base string?  That is, does it end with
 * a slash?
 */
[[gnu::pure]]
bool
is_base(StringView uri) noexcept;

#endif
