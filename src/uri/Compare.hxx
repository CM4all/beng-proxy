// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <string_view>

/**
 * Compare the end of the given (unescaped) URI with the given
 * (escaped) suffix.
 *
 * @return the beginning of the suffix within #uri or nullptr on
 * mismatch
 */
[[gnu::pure]]
const char *
UriFindUnescapedSuffix(std::string_view uri, std::string_view suffix) noexcept;
