// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "util/IntrusiveHashArrayTrie.hxx"
#include "util/ShallowCopy.hxx"
#include "util/djb_hash.hxx"

#include <utility>

struct pool;
class AllocatorPtr;

/**
 * A key string plus a precalculated hash.  This can be used to
 * calculate hashes of well-known keys at compile time.
 */
struct StringMapKey {
	std::size_t hash;
	const char *string;

	constexpr StringMapKey(const char *s) noexcept
		:hash(djb_hash_string(s)), string(s) {}
};

/**
 * String hash map.
 */
class StringMap {
	struct Item : IntrusiveHashArrayTrieHook<> {
		const char *key, *value;

		Item(const char *_key, const char *_value) noexcept
			:key(_key), value(_value) {}

		Item(ShallowCopy, const Item &src) noexcept
			:key(src.key), value(src.value) {}

		Item(const Item &) = delete;
		Item &operator=(const Item &) = delete;

		struct Hash {
			std::size_t operator()(const char *k) const noexcept {
				return djb_hash_string(k);
			}

			constexpr std::size_t operator()(StringMapKey k) const noexcept {
				return k.hash;
			}
		};

		struct Equal {
			[[gnu::pure]]
			bool operator()(const char *a, const char *b) const noexcept;

			std::size_t operator()(StringMapKey a, const char *b) const noexcept {
				return operator()(a.string, b);
			}
		};

		struct GetKey {
			const char *operator()(const Item &item) const noexcept {
				return item.key;
			}
		};

		class Cloner {
			struct pool &pool;

		public:
			explicit Cloner(struct pool &_pool) noexcept:pool(_pool) {}

			Item *operator()(const Item &src) const noexcept;
		};

		class ShallowCloner {
			struct pool &pool;

		public:
			explicit ShallowCloner(struct pool &_pool) noexcept:pool(_pool) {}

			Item *operator()(const Item &src) const noexcept;
		};
	};


	using Map = IntrusiveHashArrayTrie<Item,
					   IntrusiveHashArrayTrieOperators<Item,
									   Item::GetKey,
									   Item::Hash,
									   Item::Equal>>;

	Map map;

public:
	using const_iterator = Map::const_iterator;
	using equal_iterator = Map::equal_iterator;

	StringMap() = default;

	template<typename A>
	explicit StringMap(A &&_alloc,
			   std::initializer_list<std::pair<const char *, const char *>> init) noexcept
	{
		for (const auto &i : init)
			Add(_alloc, i.first, i.second);
	}

	StringMap(struct pool &_pool, const StringMap &src) noexcept;
	StringMap(struct pool &_pool, const StringMap *src) noexcept;

	/**
	 * Copy string pointers from #src.
	 */
	StringMap(ShallowCopy, struct pool &_pool, const StringMap &src) noexcept;

	StringMap(const StringMap &) = delete;

	StringMap(StringMap &&src) = default;

	/**
	 * Move-assign all items.  Note that this does not touch the pool;
	 * this operation is only safe if both instances use the same
	 * pool.
	 */
	StringMap &operator=(StringMap &&src) noexcept {
		map.swap(src.map);
		return *this;
	}

	const_iterator begin() const noexcept {
		return map.begin();
	}

	const_iterator end() const noexcept {
		return map.end();
	}

	[[gnu::pure]]
	bool IsEmpty() const noexcept {
		return map.empty();
	}

	void Clear() noexcept;

	void Add(AllocatorPtr alloc, StringMapKey key, const char *value) noexcept;
	const char *Set(AllocatorPtr alloc,
			StringMapKey key, const char *value) noexcept;
	const char *Remove(StringMapKey key) noexcept;

	/**
	 * Remove all existing values with the specified key and
	 * (optionally, if not nullptr) add a new value.
	 */
	void SecureSet(AllocatorPtr alloc,
		       StringMapKey key, const char *value) noexcept;

	[[gnu::pure]]
	const char *Get(const char *key) const noexcept;

	[[gnu::pure]]
	const char *Get(StringMapKey key) const noexcept;

	[[gnu::pure]]
	bool Contains(const auto &key) const noexcept {
		return Get(key) != nullptr;
	}

	[[gnu::pure]]
	std::pair<equal_iterator, equal_iterator> EqualRange(StringMapKey key) const noexcept;

	void ForEach(const char *key, std::invocable<const char *> auto f) const {
		map.for_each(key, [&f](const Item &item){
			f(item.value);
		});
	}

	void CopyFrom(AllocatorPtr alloc,
		      const StringMap &src, const char *key) noexcept;

	/**
	 * Copy string pointers with keys from the given key list.
	 *
	 * @param keys a nullptr terminated array of keys
	 */
	void ListCopyFrom(AllocatorPtr alloc,
			  const StringMap &src, const char *const*keys) noexcept;

	/**
	 * Copy string pointers with the given key prefix.
	 *
	 * @param keys a nullptr terminated array of keys
	 */
	void PrefixCopyFrom(AllocatorPtr alloc,
			    const StringMap &src, const char *prefix) noexcept;

	/**
	 * Move items from #src, merging it into this object.
	 */
	void Merge(StringMap &&src) noexcept {
		src.map.clear_and_dispose([this](Item *item){
			map.insert(*item);
		});
	}
};

[[gnu::malloc]]
StringMap *
strmap_new(struct pool *pool) noexcept;

[[gnu::malloc]]
StringMap *
strmap_dup(struct pool *pool, const StringMap *src) noexcept;

/**
 * This variation of StringMap::Get() allows the caller to pass
 * map=nullptr.
 */
[[gnu::pure]]
static inline const char *
strmap_get_checked(const StringMap *map, const char *key) noexcept
{
	return map != nullptr
		? map->Get(key)
		: nullptr;
}
