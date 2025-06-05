// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <string_view>

#include <assert.h>
#include <stddef.h>

struct escape_class {
	/**
	 * Find the first character that must be unescaped.  Returns nullptr
	 * when the string can be used as-is without unescaping.
	 */
	const char *(*unescape_find)(std::string_view p) noexcept;

	/**
	 * Unescape the given string into the output buffer.  Returns the
	 * number of characters in the output buffer.
	 */
	size_t (*unescape)(std::string_view p, char *q) noexcept;

	/**
	 * Find the first character that must be escaped.  Returns nullptr
	 * when there are no such characters.
	 */
	const char *(*escape_find)(std::string_view p) noexcept;

	/**
	 * Returns the escape string for the specified character.
	 */
	std::string_view (*escape_char)(char ch) noexcept;

	/**
	 * Measure the minimum buffer size for escaping the given string.
	 * Returns 0 when no escaping is needed.
	 */
	size_t (*escape_size)(std::string_view p) noexcept;

	/**
	 * Escape the given string into the output buffer.  Returns the
	 * number of characters in the output buffer.
	 */
	size_t (*escape)(std::string_view p, char *q) noexcept;
};

[[gnu::pure]]
static inline const char *
unescape_find(const struct escape_class *cls, std::string_view p) noexcept
{
	assert(cls != nullptr);
	assert(cls->unescape_find != nullptr);

	return cls->unescape_find(p);
}

static inline size_t
unescape_buffer(const struct escape_class *cls,
		std::string_view p, char *q) noexcept
{
	assert(cls != nullptr);
	assert(cls->unescape != nullptr);
	assert(q != nullptr);

	size_t length2 = cls->unescape(p, q);
	assert(length2 <= p.size());

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
escape_find(const struct escape_class *cls, std::string_view p) noexcept
{
	assert(cls != nullptr);
	assert(cls->escape_find != nullptr);

	return cls->escape_find(p);
}

[[gnu::pure]]
static inline size_t
escape_size(const struct escape_class *cls, std::string_view p) noexcept
{
	assert(cls != nullptr);
	assert(cls->escape_size != nullptr);

	return cls->escape_size(p);
}

[[gnu::pure]]
static inline std::string_view
escape_char(const struct escape_class *cls, char ch) noexcept
{
	assert(cls != nullptr);
	assert(cls->escape_char != nullptr);

	const auto q = cls->escape_char(ch);
	assert(q.data() != nullptr);
	return q;
}

static inline size_t
escape_buffer(const struct escape_class *cls, std::string_view p, char *q) noexcept
{
	assert(cls != nullptr);
	assert(cls->escape != nullptr);
	assert(q != nullptr);

	size_t length2 = cls->escape(p, q);
	assert(length2 >= p.size());

	return length2;
}
