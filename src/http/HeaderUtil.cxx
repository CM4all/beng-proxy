// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "HeaderUtil.hxx"
#include "util/StringStrip.hxx"

#include <string.h>

std::string_view
http_header_param(const char *value, const char *name) noexcept
{
	/* XXX this implementation only supports one param */
	const char *p = strchr(value, ';'), *q;

	if (p == nullptr)
		return {};

	p = StripLeft(p + 1);

	q = strchr(p, '=');
	if (q == nullptr || (size_t)(q - p) != strlen(name) ||
	    memcmp(p, name, q - p) != 0)
		return {};

	p = q + 1;
	if (*p == '"') {
		++p;
		q = strchr(p, '"');
		if (q == nullptr)
			return p;
		else
			return {p, size_t(q - p)};
	} else {
		return p;
	}
}
