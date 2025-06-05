// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "UnusedPtr.hxx"
#include "istream_hold.hxx"

/**
 * A variant of #UnusedIstreamPtr which wraps the #Istream with
 * istream_hold_new(), to make it safe to be used in asynchronous
 * context.
 */
class UnusedHoldIstreamPtr : public UnusedIstreamPtr {
public:
	UnusedHoldIstreamPtr() = default;
	UnusedHoldIstreamPtr(std::nullptr_t) noexcept {}

	explicit UnusedHoldIstreamPtr(struct pool &p, UnusedIstreamPtr &&_stream) noexcept
		:UnusedIstreamPtr(_stream
				  ? istream_hold_new(p, std::move(_stream))
				  : nullptr) {}

	UnusedHoldIstreamPtr(UnusedHoldIstreamPtr &&src) = default;

	UnusedHoldIstreamPtr &operator=(UnusedHoldIstreamPtr &&src) = default;
};
