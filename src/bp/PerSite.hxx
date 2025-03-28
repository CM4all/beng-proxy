// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "util/IntrusiveHashSet.hxx"
#include "util/IntrusiveList.hxx"
#include "util/SharedLease.hxx"
#include "util/StringWithHash.hxx"
#include "util/TokenBucket.hxx"

#include <cassert>
#include <string>

class BpPerSite final
	: public IntrusiveHashSetHook<>,
	  public IntrusiveListHook<IntrusiveHookMode::TRACK>,
	  SharedAnchor
{
	friend class SharedLeasePtr<BpPerSite>;
	friend class BpPerSiteMap;

	const std::string site;
	const std::size_t hash;

	TokenBucket request_count_throttle;

public:
	explicit BpPerSite(StringWithHash _site) noexcept
		:site(_site.value), hash(_site.hash) {}

	~BpPerSite() noexcept {
		assert(IsAbandoned());
	}

	struct GetSite {
		[[gnu::pure]]
		StringWithHash operator()(const BpPerSite &per_site) const noexcept {
			return StringWithHash{per_site.site, per_site.hash};
		}
	};

	bool CheckRequestCount(TokenBucketConfig config, double now) noexcept {
		return request_count_throttle.Check(config, now, 1);
	}

	bool IsExpired(double now) const noexcept {
		return request_count_throttle.IsZero(now);
	}

protected:
	// virtual methods from SharedAnchor
	void OnAbandoned() noexcept override;

private:
	bool IsLinked() const noexcept {
		return IntrusiveListHook::is_linked();
	}
};

class BpPerSiteMap {
	IntrusiveHashSet<BpPerSite, 65536,
			 IntrusiveHashSetOperators<BpPerSite, BpPerSite::GetSite,
						   std::hash<StringWithHash>,
						   std::equal_to<StringWithHash>>> map;

	IntrusiveList<BpPerSite> lru;

public:
	~BpPerSiteMap() noexcept;

	void Expire(double now) noexcept;

	[[gnu::pure]]
	SharedLeasePtr<BpPerSite> Make(StringWithHash site) noexcept;
};
