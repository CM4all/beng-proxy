// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "util/IntrusiveHashSet.hxx"
#include "util/IntrusiveList.hxx"
#include "util/TokenBucket.hxx"

#include <string>

class BpPerSite : public IntrusiveHashSetHook<>, public IntrusiveListHook<> {
	const std::string site;

	TokenBucket request_count_throttle;

public:
	explicit BpPerSite(std::string_view _site) noexcept
		:site(_site) {}

	struct GetSite {
		[[gnu::pure]]
		std::string_view operator()(const BpPerSite &per_site) const noexcept {
			return per_site.site;
		}
	};

	struct Hash {
		[[gnu::pure]]
		std::size_t operator()(std::string_view site) const noexcept;
	};

	bool CheckRequestCount(double now, double rate, double burst) noexcept {
		return request_count_throttle.Check(now, rate, burst, 1);
	}

	bool IsExpired(double now) const noexcept {
		return request_count_throttle.IsZero(now);
	}
};

class BpPerSiteMap {
	IntrusiveHashSet<BpPerSite, 65536,
			 IntrusiveHashSetOperators<BpPerSite, BpPerSite::GetSite,
						   BpPerSite::Hash,
						   std::equal_to<std::string_view>>> map;

	IntrusiveList<BpPerSite> lru;

public:
	~BpPerSiteMap() noexcept;

	void Expire(double now) noexcept;

	[[gnu::pure]]
	BpPerSite &Make(std::string_view site) noexcept;
};
