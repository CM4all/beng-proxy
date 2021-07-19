/*
 * Copyright 2007-2021 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "Id.hxx"
#include "random.hxx"
#include "util/HexFormat.hxx"

#include <assert.h>

void
SessionId::Generate() noexcept
{
	for (auto &i : data)
		i = random_uint64();
}

static auto
ToClusterNode(uint64_t id,
	      unsigned cluster_size, unsigned cluster_node) noexcept
{
	uint64_t remainder = id % (uint64_t)cluster_size;
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
	if (s.size() != sizeof(data) * 2)
		return false;

	const char *p = s.data();

	constexpr size_t segment_size = sizeof(data.front()) * 2;

	for (auto &i : data) {
		uint64_t value = 0;

		for (unsigned j = 0; j < segment_size; ++j) {
			const char ch = *p++;

			uint_least8_t digit;
			if (ch >= '0' && ch <= '9')
				digit = ch - '0';
			else if (ch >= 'a' && ch <= 'f')
				digit = ch - 'a' + 0xa;
			else
				return false;

			value = (value << 4) | digit;
		}

		i = value;
	}

	return true;
}

StringBuffer<sizeof(SessionId::data) * 2 + 1>
SessionId::Format() const noexcept
{
	StringBuffer<sizeof(data) * 2 + 1> result;

	char *p = result.data();
	for (const auto i : data)
		p = format_uint64_hex_fixed(p, i);

	*p = 0;
	return result;
}
