// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "istream_string.hxx"
#include "istream_memory.hxx"
#include "UnusedPtr.hxx"
#include "util/SpanCast.hxx"

UnusedIstreamPtr
istream_string_new(struct pool &pool, std::string_view s) noexcept
{
	return istream_memory_new(pool, AsBytes(s));
}
