// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "AllocatorPtr.hxx"
#include "util/StaticVector.hxx"

#include <string_view>
#include <vector>

template<size_t MAX>
class PoolStringBuilder {
	StaticVector<std::string_view, MAX> items;

	/**
	 * If we have more than #MAX items, they will land in this
	 * dynamic std::vector.  This usually doesn't happen, but just
	 * in case there are many APPEND/PAIR packets to be
	 * appended...
	 */
	std::vector<std::string_view> overflow;

public:
	void push_back(std::string_view s) noexcept {
		if (items.full()) [[unlikely]]
			overflow.push_back(s);
		else
			items.push_back(s);
	}

	template<typename... Args>
	void emplace_back(Args&&... args) noexcept {
		if (items.full()) [[unlikely]]
			overflow.emplace_back(std::forward<Args>(args)...);
		else
			items.emplace_back(std::forward<Args>(args)...);
	}

	char *operator()(AllocatorPtr alloc) const noexcept {
		return alloc.Concat(items, overflow);
	}

	std::string_view MakeView(AllocatorPtr alloc) const noexcept {
		return alloc.ConcatView(items, overflow);
	}
};
