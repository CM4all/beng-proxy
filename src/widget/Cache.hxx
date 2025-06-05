// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "Class.hxx"
#include "pool/Holder.hxx"
#include "util/StringLess.hxx"

#include <map>

class WidgetClassCache final : PoolHolder {
	struct Item final : PoolHolder {
		const WidgetClass cls;

		Item(PoolPtr &&_pool, const WidgetClass &_cls) noexcept;
	};

	std::map<const char *, Item, StringLess> map;

public:
	explicit WidgetClassCache(struct pool &parent_pool) noexcept;

	const WidgetClass *Get(const char *name) const noexcept {
		auto i = map.find(name);
		return i != map.end()
			? &i->second.cls
			: nullptr;
	}

	void Put(const char *name, const WidgetClass &cls) noexcept;

	void Clear() noexcept {
		map.clear();
	}
};
