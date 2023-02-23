// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * HTTP definitions according to RFC 2616 2.2.
 */

#pragma once

constexpr bool
char_is_http_char(char ch) noexcept
{
	return (ch & 0x80) == 0;
}

constexpr bool
char_is_http_ctl(char ch) noexcept
{
	return (((unsigned char)ch) <= 0x1f) || ch == 0x7f;
}

constexpr bool
char_is_http_text(char ch) noexcept
{
	return !char_is_http_ctl(ch);
}

constexpr bool
char_is_http_sp(char ch) noexcept
{
	return ch == ' ';
}

constexpr bool
char_is_http_ht(char ch) noexcept
{
	return ch == '\t';
}

constexpr bool
char_is_http_separator(char ch) noexcept
{
	return ch == '(' || ch == ')' || ch == '<' || ch == '>' ||
		ch == '@' || ch == ',' || ch == ';' || ch == ':' ||
		ch == '\\' || ch == '"' || ch == '/' ||
		ch == '[' || ch == ']' ||
		ch == '?' || ch == '=' || ch == '{' || ch == '}' ||
		char_is_http_sp(ch) || char_is_http_ht(ch);
}

constexpr bool
char_is_http_token(char ch) noexcept
{
	return char_is_http_char(ch) && !char_is_http_ctl(ch) &&
		!char_is_http_separator(ch);
}
