// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Wildcard.hxx"

#include <string.h>

std::string
MakeCommonNameWildcard(const char *s)
{
	const char *p = s;
	while (*p == '.' || *p == '*')
		++p;

	if (p > s && p[-1] != '.')
		return std::string();

	const char *dot = strchr(p, '.');
	if (dot == nullptr)
		return std::string();

	std::string result(s, p);
	result.push_back('*');
	result.append(dot);
	return result;
}
