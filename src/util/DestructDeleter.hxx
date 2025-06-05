// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

/**
 * A deleter which calls the destructor but nothing else.  This can be
 * used for objects whose underlying allocations will be freed
 * automatically.
 */
class DestructDeleter {
public:
	template<typename T>
	void operator()(T *t) noexcept {
		DeleteFromPool(t);
	}
};
