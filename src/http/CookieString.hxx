// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * Cookie string utilities according to RFC 6265 4.1.1.
 */

#pragma once

#include <string_view>
#include <utility>

std::string_view
cookie_next_unquoted_value(std::string_view &input) noexcept;

std::string_view
cookie_next_rfc_ignorant_value(std::string_view &input) noexcept;

/**
 * Like cookie_next_name_value(), but do not unquote.  Therefore, it
 * does not allocate memory and does not copy data, it just returns a
 * pointer inside the input string.
 */
std::pair<std::string_view, std::string_view>
cookie_next_name_value_raw(std::string_view &input, bool rfc_ignorant) noexcept;
