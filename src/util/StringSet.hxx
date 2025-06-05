// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "util/IntrusiveForwardList.hxx"

class AllocatorPtr;

/**
 * An unordered set of strings.
 */
class StringSet {
	struct Item : IntrusiveForwardListHook {
		const char *value;
	};

	using List = IntrusiveForwardList<Item>;

	List list;

public:
	StringSet() = default;
	StringSet(const StringSet &) = delete;
	StringSet(StringSet &&) = default;
	StringSet &operator=(const StringSet &) = delete;
	StringSet &operator=(StringSet &&src) = default;

	void Init() noexcept {
		list.clear();
	}

	[[gnu::pure]]
	bool IsEmpty() const noexcept {
		return list.empty();
	}

	[[gnu::pure]]
	bool Contains(const char *p) const noexcept;

	/**
	 * Add a string to the set.  It does not check whether the string
	 * already exists.
	 *
	 * @param p the string value which must be allocated by the caller
	 * @param alloc an allocator that is used to allocate the node
	 * (not the value)
	 */
	void Add(AllocatorPtr alloc, const char *p) noexcept;

	/**
	 * Copy all strings from one set to this, creating duplicates of
	 * all values from the specified allocator.
	 */
	void CopyFrom(AllocatorPtr alloc, const StringSet &s) noexcept;

	class const_iterator {
		List::const_iterator i;

	public:
		constexpr const_iterator(List::const_iterator _i) noexcept:i(_i) {}

		constexpr bool operator!=(const const_iterator &other) const noexcept {
			return i != other.i;
		}

		constexpr const char *operator*() const noexcept {
			return i->value;
		}

		const_iterator &operator++() noexcept {
			++i;
			return *this;
		}
	};

	const_iterator begin() const noexcept {
		return list.begin();
	}

	const_iterator end() const noexcept {
		return const_iterator{list.end()};
	}
};
