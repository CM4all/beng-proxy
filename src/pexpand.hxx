// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * Utilities for dealing with regular expressions.
 */

#pragma once

class AllocatorPtr;
class MatchData;

/**
 * Throws Pcre::Error on error.
 */
const char *
expand_string(AllocatorPtr alloc, const char *src,
	      const MatchData &match_data);

/**
 * Like expand_string(), but unescape the substitutions with the '%'
 * URI method.
 *
 * Throws Pcre::Error on error.
 */
const char *
expand_string_unescaped(AllocatorPtr alloc, const char *src,
			const MatchData &match_data);
