// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * Classify characters in a HTML/XML document.
 */

#pragma once

#include "util/CharUtil.hxx"

constexpr bool
is_html_name_start_char(char ch) noexcept
{
	return IsAlphaASCII(ch) ||
		ch == ':' || ch == '_';
}

constexpr bool
is_html_name_char(char ch) noexcept
{
	return is_html_name_start_char(ch) || IsDigitASCII(ch) ||
		ch == '-' || ch == '.';
}
