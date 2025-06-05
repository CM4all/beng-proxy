// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

/*
 * HTTP string utilities according to RFC 2616 2.2.
 */

#pragma once

#include <string_view>

std::string_view
http_next_token(std::string_view &input) noexcept;

/**
 * Like http_next_quoted_string(), but do not unquote.  Therefore, it
 * does not allocate memory and does not copy data, it just returns a
 * pointer inside the input string.
 */
std::string_view
http_next_quoted_string_raw(std::string_view &input) noexcept;
