// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

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

/**
 * Is this a character which continues a tag name?  The HTML tokenizer
 * ends a tag name only at whitespace, '/' and '>'; the '?' is ours,
 * for processing instructions.
 */
constexpr bool
is_html_tag_name_char(char ch) noexcept
{
	return !IsWhitespaceOrNull(ch) &&
		ch != '/' && ch != '?' && ch != '>';
}

/**
 * Is this a character which belongs to an attribute name?
 */
constexpr bool
is_html_attribute_name_char(char ch) noexcept
{
	return !IsWhitespaceOrNull(ch) &&
		ch != '/' && ch != '>' && ch != '=';
}
