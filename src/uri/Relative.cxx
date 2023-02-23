// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Relative.hxx"
#include "uri/Extract.hxx"
#include "util/StringCompare.hxx"

using std::string_view_literals::operator""sv;

std::string_view
uri_relative(std::string_view base, std::string_view uri) noexcept
{
	if (base.empty() || uri.empty())
		return {};

	if (SkipPrefix(uri, base))
		return uri;

	/* special case: http://hostname without trailing slash */
	if (uri.size() == base.size() - 1 &&
	    base.starts_with(uri) &&
	    base.back() == '/' &&
	    UriAfterScheme(uri).data() != nullptr &&
	    UriAfterScheme(uri).find('/') == std::string_view::npos)
		return ""sv;

	return {};
}
