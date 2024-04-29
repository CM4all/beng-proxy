// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * Cookie string utilities according to RFC 6265 4.1.1.
 */

#pragma once

#include <string_view>
#include <utility>

std::pair<std::string_view, std::string_view>
cookie_next_name_value_raw(std::string_view &input, bool rfc_ignorant) noexcept;
