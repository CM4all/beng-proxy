// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "pool.hxx"

/**
 * Temporary memory pool.
 */
extern struct pool *tpool_singleton;

extern unsigned tpool_users;

class TempPoolLease {
public:
	TempPoolLease() noexcept {
		++tpool_users;
	}

	~TempPoolLease() noexcept {
		if (--tpool_users == 0)
			pool_clear(*tpool_singleton);
	}

	operator struct pool &() const noexcept {
		return *tpool_singleton;
	}

	operator struct pool *() const noexcept {
		return tpool_singleton;
	}
};

void
tpool_init(struct pool *parent) noexcept;

void
tpool_deinit() noexcept;
