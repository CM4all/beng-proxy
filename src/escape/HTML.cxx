// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "HTML.hxx"
#include "Class.hxx"
#include "util/CharUtil.hxx"
#include "util/HexParse.hxx"
#include "util/StringSplit.hxx"
#include "util/UTF8.hxx"

#include <assert.h>
#include <string.h>

using std::string_view_literals::operator""sv;

[[gnu::pure]]
static const char *
html_unescape_find(std::string_view p) noexcept
{
	const auto i = p.find('&');
	return i != p.npos
		? p.data() + i
		: nullptr;
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
html_unescape(std::string_view src, char *q) noexcept
{
	const char *const q_start = q;

	while (true) {
		const auto [before_ampersand, after_ampersand] = Split(src, '&');

		memmove(q, before_ampersand.data(), before_ampersand.size());
		q += before_ampersand.size();

		if (after_ampersand.data() == nullptr)
			break;

		auto [entity, rest] = Split(after_ampersand, ';');
		if (rest.data() == nullptr || entity.empty()) {
			*q++ = '&';
			src = after_ampersand;
			continue;
		}

		if (entity == "amp"sv)
			*q++ = '&';
		else if (entity == "quot"sv)
			*q++ = '"';
		else if (entity == "lt"sv)
			*q++ = '<';
		else if (entity == "gt"sv)
			*q++ = '>';
		else if (entity == "apos"sv)
			*q++ = '\'';
		else if (entity.front() == '#') {
			entity = entity.substr(1);

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
html_escape_size(std::string_view _p) noexcept
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
html_escape_find(std::string_view _p) noexcept
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

static std::string_view
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
		return {};
	}
}

static size_t
html_escape(std::string_view _p, char *q) noexcept
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
