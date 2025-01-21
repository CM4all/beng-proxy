// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "AllocatorPtr.hxx"
#include "util/StaticVector.hxx"

#include <string_view>

template<size_t MAX>
class PoolStringBuilder {
	StaticVector<std::string_view, MAX> items;

public:
	void push_back(std::string_view s) noexcept {
		items.push_back(s);
	}

	template<typename... Args>
	void emplace_back(Args&&... args) noexcept {
		items.emplace_back(std::forward<Args>(args)...);
	}

	char *operator()(AllocatorPtr alloc) const noexcept {
		return alloc.Concat(items);
	}

	std::string_view MakeView(AllocatorPtr alloc) const noexcept {
		return alloc.ConcatView(items);
	}
};
