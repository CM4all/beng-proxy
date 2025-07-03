// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

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
	TokenBucket request_traffic_throttle;

	double expires = 0;

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
		bool result = request_count_throttle.Check(config, now, 1);

		if (double full_time = request_count_throttle.GetFullTime(config);
		    full_time > expires)
			expires = full_time;

		return result;
	}

	bool CheckRequestTraffic(double now) const noexcept {
		return request_traffic_throttle.IsZero(now);
	}

	void UpdateRequestTraffic(TokenBucketConfig config, double now, double size) noexcept {
		request_traffic_throttle.Update(config, now, size);

		if (double full_time = request_traffic_throttle.GetFullTime(config);
		    full_time > expires)
			expires = full_time;
	}

	bool IsExpired(double now) const noexcept {
		return now >= expires;
	}

	void ResetLimiter() noexcept {
		request_count_throttle.Reset();
		request_traffic_throttle.Reset();
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

	/**
	 * Look up an existing #BpPerSite item.  Returns nullptr if
	 * the site does not exist.
	 */
	[[gnu::pure]]
	BpPerSite *Get(StringWithHash site) noexcept;

	/**
	 * Like Get(), but create an item if it does not exist and
	 * wrap it in a #SharedLeasePtr.
	 */
	[[gnu::pure]]
	SharedLeasePtr<BpPerSite> Make(StringWithHash site) noexcept;
};
