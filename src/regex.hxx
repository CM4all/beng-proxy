// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * Utilities for dealing with regular expressions.
 */

#pragma once

#include <cstddef>
#include <string_view>

class MatchData;

/**
 * Calculate the length of an expanded string.
 *
 * Throws Pcre::Error on error.
 *
 * @return the length (without the null terminator)
 */
std::size_t
ExpandStringLength(std::string_view src, const MatchData &match_data);
