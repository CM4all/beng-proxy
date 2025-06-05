// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <string_view>

static bool
IsAcmeInvalid(std::string_view s) noexcept
{
	using std::string_view_literals::operator""sv;
	return s.ends_with(".acme.invalid"sv);
}
