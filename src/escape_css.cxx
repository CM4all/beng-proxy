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

#include "escape_css.hxx"
#include "escape_class.hxx"

#include <assert.h>
#include <string.h>

static const char *
css_unescape_find(StringView p) noexcept
{
	return p.Find('\\');
}

static constexpr bool
need_simple_escape(char ch) noexcept
{
	return ch == '\\' || ch == '"' || ch == '\'';
}

static size_t
css_unescape(StringView _p, char *q) noexcept
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
css_escape_size(StringView _p) noexcept
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
css_escape_find(StringView _p) noexcept
{
	const char *p = _p.begin(), *const end = _p.end();

	while (p < end) {
		if (need_simple_escape(*p))
			return p;

		++p;
	}

	return nullptr;
}

static StringView
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
		return nullptr;
	}
}

static size_t
css_escape(StringView _p, char *q) noexcept
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
