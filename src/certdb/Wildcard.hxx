// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <string>

/**
 * Attempt to convert the given "common name" (i.e. host name) to a
 * wild card by replacing the first segment with an asterisk.  Returns
 * an empty string if this is not possible.
 */
std::string
MakeCommonNameWildcard(const char *s);
