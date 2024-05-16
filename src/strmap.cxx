// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "strmap.hxx"
#include "util/StringAPI.hxx"
#include "util/StringCompare.hxx"
#include "AllocatorPtr.hxx"

#include <iterator>
#include <string_view>

bool
StringMap::Item::Equal::operator()(const char *a, const char *b) const noexcept
{
	return StringIsEqual(a, b);
}

StringMap::Item *
StringMap::Item::Cloner::operator()(const Item &src) const noexcept
{
	return NewFromPool<Item>(pool,
				 p_strdup(&pool, src.key),
				 p_strdup(&pool, src.value));
}

StringMap::Item *
StringMap::Item::ShallowCloner::operator()(const Item &src) const noexcept
{
	return NewFromPool<Item>(pool, ShallowCopy(), src);
}

StringMap::StringMap(struct pool &pool, const StringMap &src) noexcept
{
	for (const auto &i : src)
		Add(pool, p_strdup(pool, i.key), p_strdup(pool, i.value));
}

StringMap::StringMap(struct pool &pool, const StringMap *src) noexcept
{
	if (src != nullptr)
		for (const auto &i : *src)
			Add(pool, p_strdup(pool, i.key), p_strdup(pool, i.value));
}

StringMap::StringMap(ShallowCopy, struct pool &pool,
		     const StringMap &src) noexcept
{
	for (const auto &i : src)
		Add(pool, i.key, i.value);
}

void
StringMap::Clear() noexcept
{
	map.clear_and_dispose(NoPoolDisposer());
}

void
StringMap::Add(AllocatorPtr alloc,
	       const char *key, const char *value) noexcept
{
	Item *item = alloc.New<Item>(key, value);
	map.insert(*item);
}

const char *
StringMap::Set(AllocatorPtr alloc, const char *key, const char *value) noexcept
{
	if (auto i = map.find(key); i != map.end()) {
		const char *old_value = i->value;
		i->value = value;
		return old_value;
	} else {
		// optimize by reusing lookup results?
		Add(alloc, key, value);
		return nullptr;
	}
}

const char *
StringMap::Remove(const StringMapKey key) noexcept
{
	auto i = map.find(key);
	if (i == map.end())
		return nullptr;

	const char *value = i->value;
	map.erase_and_dispose(i, NoPoolDisposer());
	return value;
}

void
StringMap::SecureSet(AllocatorPtr alloc,
		     const char *key, const char *value) noexcept
{
	/* remove all items with the specified key, but reuse one of
	   them */
	Item *item = nullptr;
	map.remove_and_dispose_key(key, [&item](Item *i){
		item = i;
	});

	if (value == nullptr)
		return;

	if (item == nullptr)
		item = alloc.New<Item>(key, value);
	else
		item->value = value;

	map.insert(*item);
}

const char *
StringMap::Get(const char *key) const noexcept
{
	auto i = map.find(key);
	if (i == map.end())
		return nullptr;

	return i->value;
}

const char *
StringMap::Get(const StringMapKey key) const noexcept
{
	auto i = map.find(key);
	if (i == map.end())
		return nullptr;

	return i->value;
}

std::pair<StringMap::equal_iterator, StringMap::equal_iterator>
StringMap::EqualRange(const StringMapKey key) const noexcept
{
	return map.equal_range(key);
}

void
StringMap::CopyFrom(AllocatorPtr alloc,
		    const StringMap &src, const char *key) noexcept
{
	src.ForEach(key, [this, &alloc, key](const char *value){
		Add(alloc, key, value);
	});
}

void
StringMap::ListCopyFrom(AllocatorPtr alloc,
			const StringMap &src, const char *const*keys) noexcept
{
	assert(keys != nullptr);

	for (; *keys != nullptr; ++keys)
		CopyFrom(alloc, src, *keys);
}

void
StringMap::PrefixCopyFrom(AllocatorPtr alloc,
			  const StringMap &src, const char *_prefix) noexcept
{
	assert(_prefix != nullptr);
	assert(*_prefix != 0);

	const std::string_view prefix{_prefix};

	// TODO optimize this search
	for (const auto &i : src)
		if (StringStartsWith(i.key, prefix))
			Add(alloc, i.key, i.value);
}

StringMap *
strmap_new(struct pool *pool) noexcept
{
	return NewFromPool<StringMap>(*pool);
}

StringMap *
strmap_dup(struct pool *pool, const StringMap *src) noexcept
{
	return NewFromPool<StringMap>(*pool, *pool, *src);
}
