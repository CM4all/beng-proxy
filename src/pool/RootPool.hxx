// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

class RootPool {
	struct pool &p;

public:
	RootPool();
	~RootPool();

	RootPool(const RootPool &) = delete;
	RootPool &operator=(const RootPool &) = delete;

	struct pool &get() {
		return p;
	}

	operator struct pool &() {
		return p;
	}

	operator struct pool *() {
		return &p;
	}
};
