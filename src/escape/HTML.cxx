/*
 * Copyright 2007-2022 CM4all GmbH
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

#include "HTML.hxx"
#include "Class.hxx"
#include "util/CharUtil.hxx"
#include "util/HexParse.hxx"
#include "util/StringView.hxx"
#include "util/UTF8.hxx"

#include <assert.h>
#include <string.h>

[[gnu::pure]]
static const char *
html_unescape_find(StringView p) noexcept
{
	return p.Find('&');
}

[[gnu::pure]]
static std::pair<uint_least32_t, std::string_view>
ParseDecimal(std::string_view s) noexcept
{
	uint_least32_t value = 0;

	for (auto i = s.begin(), end = s.end(); i != end; ++i) {
		const char ch = *i;
		if (!IsDigitASCII(ch))
			return {value, {i, end}};

		value = value * 10 + (ch - '0');
	}

	return {value, {}};
}

[[gnu::pure]]
static std::pair<uint_least32_t, std::string_view>
ParseHex(std::string_view s) noexcept
{
	uint_least32_t value = 0;

	for (auto i = s.begin(), end = s.end(); i != end; ++i) {
		const char ch = *i;
		int d = ParseHexDigit(ch);
		if (d < 0)
			return {value, {i, end}};

		value = value * 0x10 + d;
	}

	return {value, {}};
}

[[gnu::pure]]
static std::pair<uint_least32_t, std::string_view>
ParseNumericEntity(std::string_view entity) noexcept
{
	assert(!entity.empty());

	if (entity.front() == 'x')
		return ParseHex(entity.substr(1));

	return ParseDecimal(entity);
}

static size_t
html_unescape(StringView src, char *q) noexcept
{
	const char *const q_start = q;

	while (true) {
		const auto [before_ampersand, after_ampersand] = src.Split('&');

		memmove(q, before_ampersand.data, before_ampersand.size);
		q += before_ampersand.size;

		if (after_ampersand == nullptr)
			break;

		auto [entity, rest] = after_ampersand.Split(';');
		if (rest == nullptr || entity.empty()) {
			*q++ = '&';
			src = after_ampersand;
			continue;
		}

		if (entity.Equals("amp"))
			*q++ = '&';
		else if (entity.Equals("quot"))
			*q++ = '"';
		else if (entity.Equals("lt"))
			*q++ = '<';
		else if (entity.Equals("gt"))
			*q++ = '>';
		else if (entity.Equals("apos"))
			*q++ = '\'';
		else if (entity.front() == '#') {
			entity.pop_front();

			if (entity.empty()) {
				*q++ = '&';
				src = after_ampersand;
				continue;
			}

			auto [value, unparsed] = ParseNumericEntity(entity);
			if (value <= 0 || value > 0x10ffff ||
			    !unparsed.empty()) {
				*q++ = '&';
				src = after_ampersand;
				continue;
			}

			q = UnicodeToUTF8(value, q);
		} else {
			*q++ = '&';
			src = after_ampersand;
			continue;
		}

		src = rest;
	}

	return q - q_start;
}

static size_t
html_escape_size(StringView _p) noexcept
{
	const char *p = _p.begin(), *const end = _p.end();

	size_t size = 0;
	while (p < end) {
		switch (*p++) {
		case '&':
			size += 5;
			break;

		case '"':
		case '\'':
			size += 6;
			break;

		case '<':
		case '>':
			size += 4;
			break;

		default:
			++size;
		}
	}

	return size;
}

static const char *
html_escape_find(StringView _p) noexcept
{
	const char *p = _p.begin(), *const end = _p.end();

	while (p < end) {
		switch (*p) {
		case '&':
		case '"':
		case '\'':
		case '<':
		case '>':
			return p;

		default:
			++p;
		}
	}

	return nullptr;
}

static StringView
html_escape_char(char ch) noexcept
{
	switch (ch) {
	case '&':
		return "&amp;";

	case '"':
		return "&quot;";

	case '\'':
		return "&apos;";

	case '<':
		return "&lt;";

	case '>':
		return "&gt;";

	default:
		assert(false);
		return nullptr;
	}
}

static size_t
html_escape(StringView _p, char *q) noexcept
{
	const char *p = _p.begin(), *const p_end = _p.end(), *const q_start = q;

	while (p < p_end) {
		char ch = *p++;
		switch (ch) {
		case '&':
			q = (char *)mempcpy(q, "&amp;", 5);
			break;

		case '"':
			q = (char *)mempcpy(q, "&quot;", 6);
			break;

		case '\'':
			q = (char *)mempcpy(q, "&apos;", 6);
			break;

		case '<':
			q = (char *)mempcpy(q, "&lt;", 4);
			break;

		case '>':
			q = (char *)mempcpy(q, "&gt;", 4);
			break;

		default:
			*q++ = ch;
		}
	}

	return q - q_start;
}

const struct escape_class html_escape_class = {
	html_unescape_find,
	html_unescape,
	html_escape_find,
	html_escape_char,
	html_escape_size,
	html_escape,
};
