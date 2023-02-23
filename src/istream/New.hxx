// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "UnusedPtr.hxx"
#include "pool/pool.hxx"

#include <utility>

template<typename T, typename... Args>
static inline T *
NewIstream(struct pool &pool, Args&&... args)
{
	return NewFromPool<T>(pool, pool,
			      std::forward<Args>(args)...);
}

template<typename T, typename... Args>
static inline UnusedIstreamPtr
NewIstreamPtr(struct pool &pool, Args&&... args)
{
	return UnusedIstreamPtr(NewIstream<T>(pool,
					      std::forward<Args>(args)...));
}
