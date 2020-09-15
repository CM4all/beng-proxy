/*
 * Copyright 2007-2020 CM4all GmbH
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
 * Extract parts of an URI.
 */

#pragma once

#include "util/Compiler.h"

#include <utility>

struct StringView;

gcc_pure
bool
uri_has_protocol(StringView uri) noexcept;

/**
 * Return the URI part after the protocol specification (and after the
 * double slash).
 */
gcc_pure
const char *
uri_after_protocol(const char *uri) noexcept;

gcc_pure
StringView
uri_after_protocol(StringView uri) noexcept;

/**
 * Does this URI have an authority part?
 */
template<typename U>
gcc_pure
inline bool
uri_has_authority(U &&uri) noexcept
{
	return uri_after_protocol(std::forward<U>(uri)) != nullptr;
}

gcc_pure
StringView
uri_host_and_port(const char *uri) noexcept;

/**
 * Returns the URI path (including the query string) or nullptr if the
 * given URI has no path.
 */
gcc_pure
const char *
uri_path(const char *uri) noexcept;

gcc_pure
const char *
uri_query_string(const char *uri) noexcept;
