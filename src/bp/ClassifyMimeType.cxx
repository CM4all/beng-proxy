// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "ClassifyMimeType.hxx"
#include "util/StringCompare.hxx"

using std::string_view_literals::operator""sv;

[[gnu::pure]]
bool
IsTextMimeType(std::string_view type) noexcept
{
	if (type.starts_with("text/"sv)) {
		return true;
	} else if (SkipPrefix(type, "application/"sv)) {
		return type.starts_with("json"sv) ||
			type.starts_with("jose+json"sv) ||
			type.starts_with("problem+json"sv) ||
			type.starts_with("xhtml"sv) ||
			type.starts_with("xml"sv) ||
			type.starts_with("javascript"sv);
	} else {
		return false;
	}
}
