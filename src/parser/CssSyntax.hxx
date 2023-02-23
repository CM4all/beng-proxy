// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

/*
 * CSS syntax rules.
 */

#include "util/CharUtil.hxx"

constexpr bool
is_css_nonascii(char ch) noexcept
{
	return !IsASCII(ch);
}

constexpr bool
is_css_nmstart(char ch) noexcept
{
	return ch == '_' || IsAlphaASCII(ch) || is_css_nonascii(ch) ||
		ch == '\\';
}

constexpr bool
is_css_nmchar(char ch) noexcept
{
	return is_css_nmstart(ch) || IsDigitASCII(ch) || ch == '-';
}

constexpr bool
is_css_ident_start(char ch) noexcept
{
	return ch == '-' || is_css_nmstart(ch);
}

constexpr bool
is_css_ident_char(char ch) noexcept
{
	return is_css_nmchar(ch);
}
