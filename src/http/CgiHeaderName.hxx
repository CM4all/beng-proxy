// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "util/CharUtil.hxx"
#include "util/StringVerify.hxx"

/**
 * Check whether the given (lower-case) HTTP request header name can
 * be translated to a CGI environment variable ("HTTP_...").
 *
 * That translation folds all non-alphanumeric characters to an
 * underscore ("_"), which means that different header names may
 * collide.  To avoid this, this function rejects headers with weird
 * (but allowed) characters that would fold into the uderscore, except
 * for the dash ("-").
 */
[[gnu::pure]]
static inline bool
IsCgiCompatibleHeaderName(const char *name) noexcept
{
	return CheckCharsNonEmpty(name, [](char ch) noexcept {
		return IsLowerAlphaNumericASCII(ch) || ch == '-';
	});
}
