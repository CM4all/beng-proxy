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
