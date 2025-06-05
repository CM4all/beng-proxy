// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

/*
 * Cookie string utilities according to RFC 6265 4.1.1.
 */

#pragma once

#include <string_view>
#include <utility>

std::string_view
cookie_next_rfc_ignorant_value(std::string_view &input) noexcept;

std::pair<std::string_view, std::string_view>
cookie_next_name_value(std::string_view &input, bool rfc_ignorant) noexcept;
