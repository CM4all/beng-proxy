// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Static.hxx"
#include "Class.hxx"

static char buffer[4096];

const char *
unescape_static(const struct escape_class *cls, std::string_view p) noexcept
{
	if (p.size() >= sizeof(buffer))
		return nullptr;

	size_t l = unescape_buffer(cls, p, buffer);
	buffer[l] = 0;
	return buffer;
}

const char *
escape_static(const struct escape_class *cls, std::string_view p) noexcept
{
	size_t l = escape_size(cls, p);
	if (l >= sizeof(buffer))
		return nullptr;

	l = escape_buffer(cls, p, buffer);
	buffer[l] = 0;
	return buffer;
}
