// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Wildcard.hxx"

std::string
MakeCommonNameWildcard(std::string_view s) noexcept
{
	const auto dot = s.find('.');
	if (dot == 0 || dot == s.npos)
		/* there is no label which could be replaced */
		return {};

	if (s.front() == '*')
		/* already a wildcard */
		return {};

	std::string result{'*'};
	result.append(s.substr(dot));
	return result;
}
