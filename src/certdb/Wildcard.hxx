// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <string>
#include <string_view>

/**
 * Attempt to convert the given "common name" (i.e. host name) to a
 * wild card by replacing the first segment with an asterisk.  Returns
 * an empty string if this is not possible.
 *
 * This does not validate the given name; it is assumed to be a
 * syntactically valid host name (see VerifyDomainName()).
 */
[[gnu::pure]]
std::string
MakeCommonNameWildcard(std::string_view s) noexcept;
