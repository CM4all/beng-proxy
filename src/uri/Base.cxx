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

#include "Base.hxx"
#include "util/StringCompare.hxx"

#include <assert.h>

const char *
base_tail(const char *uri, StringView base) noexcept
{
	assert(uri != nullptr);

	if (!is_base(base))
		/* not a valid base */
		return nullptr;

	return StringAfterPrefix(uri, base);
}

const char *
require_base_tail(const char *uri, StringView base) noexcept
{
	assert(uri != nullptr);
	assert(is_base(base));
	assert(StringStartsWith(uri, base));

	return uri + base.size;
}

size_t
base_string(StringView uri, StringView tail) noexcept
{
	if (uri.size == tail.size)
		/* special case: zero-length prefix (not followed by a
		   slash) */
		return memcmp(uri.data, tail.data, uri.size) == 0
			? 0 : (size_t)-1;

	return uri.size > tail.size && uri[uri.size - tail.size - 1] == '/' &&
		memcmp(uri.data + uri.size - tail.size, tail.data, tail.size) == 0
		? uri.size - tail.size
		: (size_t)-1;
}

bool
is_base(StringView uri) noexcept
{
	return !uri.empty() && uri.back() == '/';
}
