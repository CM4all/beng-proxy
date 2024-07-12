// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "PerSite.hxx"
#include "util/DeleteDisposer.hxx"
#include "util/djb_hash.hxx"
#include "util/SpanCast.hxx"

inline std::size_t
BpPerSite::Hash::operator()(std::string_view site) const noexcept
{
	return djb_hash(AsBytes(site));
}

BpPerSiteMap::~BpPerSiteMap() noexcept
{
	map.clear_and_dispose(DeleteDisposer{});
}

void
BpPerSiteMap::Expire(double now) noexcept
{
	while (!lru.empty()) {
		auto &per_site = lru.front();
		if (!per_site.IsExpired(now))
			break;

		map.erase(map.iterator_to(per_site));
		lru.erase(lru.iterator_to(per_site));
		delete &per_site;
	}
}

BpPerSite &
BpPerSiteMap::Make(std::string_view site) noexcept
{
	auto [it, inserted] = map.insert_check(site);
	if (inserted) {
		auto *per_site = new BpPerSite(site);
		map.insert_commit(it, *per_site);
		lru.push_back(*per_site);
		return *per_site;
	} else {
		lru.erase(lru.iterator_to(*it));
		lru.push_back(*it);
		return *it;
	}
}
