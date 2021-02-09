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

#pragma once

#include "util/StringView.hxx"

#include <assert.h>
#include <stddef.h>

struct escape_class {
	/**
	 * Find the first character that must be unescaped.  Returns nullptr
	 * when the string can be used as-is without unescaping.
	 */
	const char *(*unescape_find)(StringView p) noexcept;

	/**
	 * Unescape the given string into the output buffer.  Returns the
	 * number of characters in the output buffer.
	 */
	size_t (*unescape)(StringView p, char *q) noexcept;

	/**
	 * Find the first character that must be escaped.  Returns nullptr
	 * when there are no such characters.
	 */
	const char *(*escape_find)(StringView p) noexcept;

	/**
	 * Returns the escape string for the specified character.
	 */
	StringView (*escape_char)(char ch) noexcept;

	/**
	 * Measure the minimum buffer size for escaping the given string.
	 * Returns 0 when no escaping is needed.
	 */
	size_t (*escape_size)(StringView p) noexcept;

	/**
	 * Escape the given string into the output buffer.  Returns the
	 * number of characters in the output buffer.
	 */
	size_t (*escape)(StringView p, char *q) noexcept;
};

[[gnu::pure]]
static inline const char *
unescape_find(const struct escape_class *cls, StringView p) noexcept
{
	assert(cls != nullptr);
	assert(cls->unescape_find != nullptr);

	return cls->unescape_find(p);
}

static inline size_t
unescape_buffer(const struct escape_class *cls, StringView p, char *q) noexcept
{
	assert(cls != nullptr);
	assert(cls->unescape != nullptr);
	assert(q != nullptr);

	size_t length2 = cls->unescape(p, q);
	assert(length2 <= p.size);

	return length2;
}

static inline size_t
unescape_inplace(const struct escape_class *cls,
		 char *p, size_t length) noexcept
{
	assert(cls != nullptr);
	assert(cls->unescape != nullptr);

	size_t length2 = cls->unescape({p, length}, p);
	assert(length2 <= length);

	return length2;
}

[[gnu::pure]]
static inline const char *
escape_find(const struct escape_class *cls, StringView p) noexcept
{
	assert(cls != nullptr);
	assert(cls->escape_find != nullptr);

	return cls->escape_find(p);
}

[[gnu::pure]]
static inline size_t
escape_size(const struct escape_class *cls, StringView p) noexcept
{
	assert(cls != nullptr);
	assert(cls->escape_size != nullptr);

	return cls->escape_size(p);
}

[[gnu::pure]]
static inline StringView
escape_char(const struct escape_class *cls, char ch) noexcept
{
	assert(cls != nullptr);
	assert(cls->escape_char != nullptr);

	const auto q = cls->escape_char(ch);
	assert(!q.IsNull());
	return q;
}

static inline size_t
escape_buffer(const struct escape_class *cls, StringView p, char *q) noexcept
{
	assert(cls != nullptr);
	assert(cls->escape != nullptr);
	assert(q != nullptr);

	size_t length2 = cls->escape(p, q);
	assert(length2 >= p.size);

	return length2;
}
