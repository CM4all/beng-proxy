// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "ClassifyMimeType.hxx"

using std::string_view_literals::operator""sv;

[[gnu::pure]]
bool
IsTextMimeType(std::string_view type) noexcept
{
	return type.starts_with("text/"sv) ||
		type.starts_with("application/json"sv) ||
		type.starts_with("application/javascript"sv);
}
