// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <string_view>

[[gnu::pure]]
bool
StringListContains(std::string_view haystack, char separator,
		   std::string_view needle) noexcept;
