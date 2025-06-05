// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "CSS.hxx"
#include "Class.hxx"

#include <assert.h>
#include <string.h>

static const char *
css_unescape_find(std::string_view p) noexcept
{
	const auto i = p.find('\\');
	return i != p.npos
		? p.data() + i
		: nullptr;
}

static constexpr bool
need_simple_escape(char ch) noexcept
{
	return ch == '\\' || ch == '"' || ch == '\'';
}

static size_t
css_unescape(std::string_view _p, char *q) noexcept
{
	const char *p = _p.begin(), *const p_end = _p.end(), *const q_start = q;

	const char *bs;
	while ((bs = (const char *)memchr(p, '\\', p_end - p)) != nullptr) {
		memmove(q, p, bs - p);
		q += bs - p;

		p = bs + 1;

		if (p < p_end && need_simple_escape(*p))
			*q++ = *p++;
		else
			/* XXX implement newline and hex codes */
			*q++ = '\\';
	}

	memmove(q, p, p_end - p);
	q += p_end - p;

	return q - q_start;
}

static size_t
css_escape_size(std::string_view _p) noexcept
{
	const char *p = _p.begin(), *const end = _p.end();

	size_t size = 0;
	while (p < end) {
		if (need_simple_escape(*p))
			size += 2;
		else
			/* XXX implement newline and hex codes */
			++size;
	}

	return size;
}

static const char *
css_escape_find(std::string_view _p) noexcept
{
	const char *p = _p.begin(), *const end = _p.end();

	while (p < end) {
		if (need_simple_escape(*p))
			return p;

		++p;
	}

	return nullptr;
}

static std::string_view
css_escape_char(char ch) noexcept
{
	switch (ch) {
	case '\\':
		return "\\\\";

	case '"':
		return "\\\"";

	case '\'':
		return "\\'";

	default:
		assert(false);
		return {};
	}
}

static size_t
css_escape(std::string_view _p, char *q) noexcept
{
	const char *p = _p.begin(), *const p_end = _p.end(), *const q_start = q;

	while (p < p_end) {
		char ch = *p++;
		if (need_simple_escape(ch)) {
			*q++ = '\\';
			*q++ = ch;
		} else
			*q++ = ch;
	}

	return q - q_start;
}

const struct escape_class css_escape_class = {
	css_unescape_find,
	css_unescape,
	css_escape_find,
	css_escape_char,
	css_escape_size,
	css_escape,
};
