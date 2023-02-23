// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "AlpnSelect.hxx"
#include "AlpnIterator.hxx"

#include <assert.h>
#include <string.h>

std::span<const unsigned char>
FindAlpn(std::span<const unsigned char> haystack,
	 std::span<const unsigned char> needle) noexcept
{
	assert(!needle.empty());
	assert((size_t)needle.front() + 1 == needle.size());

	for (const auto i : AlpnRange{haystack}) {
		if (i.size() == needle.size() &&
		    memcmp(i.data(), needle.data(), needle.size()) == 0)
			return i.subspan(1);
	}

	return {};
}
