// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Id.hxx"
#include "util/HexFormat.hxx"
#include "util/HexParse.hxx"
#include "util/StringBuffer.hxx"

#include <assert.h>

static auto
ToClusterNode(uint64_t id,
	      unsigned cluster_size, unsigned cluster_node) noexcept
{
	/* use only the lower 32 bit because that is what beng-lb's
	   lb_session_get() function uses */
	const auto remainder = sticky_hash_t(id) % sticky_hash_t(cluster_size);
	assert(remainder < cluster_size);

	id -= remainder;
	id += cluster_node;
	return id;
}

void
SessionId::SetClusterNode(unsigned cluster_size,
			  unsigned cluster_node) noexcept
{
	assert(cluster_size > 0);
	assert(cluster_node < cluster_size);

	const auto old_hash = GetClusterHash();
	const auto new_hash = ToClusterNode(old_hash, cluster_size, cluster_node);
	data.back() = new_hash;
}

bool
SessionId::Parse(std::string_view s) noexcept
{
	return ParseLowerHexFixed(s, data);
}

StringBuffer<sizeof(SessionId::data) * 2 + 1>
SessionId::Format() const noexcept
{
	StringBuffer<sizeof(data) * 2 + 1> result;

	char *p = result.data();
	for (const auto i : data)
		p = HexFormatUint64Fixed(p, i);

	*p = 0;
	return result;
}
