// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "PerSite.hxx"
#include "util/DeleteDisposer.hxx"
#include "util/SpanCast.hxx"

void
BpPerSite::OnAbandoned() noexcept
{
	if (!IsLinked())
		delete this;
}

BpPerSiteMap::~BpPerSiteMap() noexcept
{
	lru.clear_and_dispose(DeleteDisposer{});
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

		if (per_site.IsAbandoned())
			delete &per_site;
	}
}

BpPerSite *
BpPerSiteMap::Get(StringWithHash site) noexcept
{
	if (auto it = map.find(site); it != map.end())
		return &*it;

	return nullptr;
}

SharedLeasePtr<BpPerSite>
BpPerSiteMap::Make(StringWithHash site) noexcept
{
	auto [it, inserted] = map.insert_check(site);
	if (inserted) {
		auto *per_site = new BpPerSite(site);
		map.insert_commit(it, *per_site);
		lru.push_back(*per_site);
		return SharedLeasePtr{*per_site};
	} else {
		lru.erase(lru.iterator_to(*it));
		lru.push_back(*it);
		return SharedLeasePtr{*it};
	}
}
