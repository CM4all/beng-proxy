// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <cstddef>
#include <string_view>

[[gnu::pure]]
bool
http_must_quote_token(std::string_view src) noexcept;

std::size_t
http_quote_string(char *dest, std::string_view src) noexcept;
