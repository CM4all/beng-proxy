// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "UnusedPtr.hxx"

#include <span>

struct pool;
class Istream;

/**
 * Concatenate several istreams.
 */
UnusedIstreamPtr
_NewConcatIstream(struct pool &pool, std::span<UnusedIstreamPtr> inputs);

template<typename... Args>
auto
NewConcatIstream(struct pool &pool, Args&&... args)
{
	UnusedIstreamPtr inputs[]{std::forward<Args>(args)...};
	return _NewConcatIstream(pool, inputs);
}

void
AppendConcatIstream(UnusedIstreamPtr &cat, UnusedIstreamPtr istream) noexcept;
