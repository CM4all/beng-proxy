// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "util/IntrusiveForwardList.hxx"

/**
 * List of key/value pairs.
 */
class KeyValueList {
public:
	struct Item : IntrusiveForwardListHook {
		const char *key, *value;

		Item(const char *_key, const char *_value) noexcept
			:key(_key), value(_value) {}
	};

private:
	using List = IntrusiveForwardList<Item>;

	typedef List::const_iterator const_iterator;

	List list;

public:
	KeyValueList() = default;
	KeyValueList(const KeyValueList &) = delete;
	KeyValueList(KeyValueList &&src) noexcept
		:list(std::move(src.list)) {}

	template<typename Alloc>
	KeyValueList(Alloc &&alloc, const KeyValueList &src) noexcept {
		for (const auto &i : src)
			Add(alloc, alloc.Dup(i.key), alloc.Dup(i.value));
	}

	KeyValueList &operator=(KeyValueList &&src) noexcept {
		list = std::move(src.list);
		return *this;
	}

	const_iterator begin() const noexcept {
		return list.begin();
	}

	const_iterator end() const noexcept {
		return list.end();
	}

	bool IsEmpty() const noexcept {
		return list.empty();
	}

	void Clear() noexcept {
		list.clear();
	}

	template<typename Alloc>
	void Add(Alloc &&alloc, const char *key, const char *value) noexcept {
		auto item = alloc.template New<Item>(key, value);
		list.push_front(*item);
	}

	void Reverse() noexcept {
		list.reverse();
	}
};
