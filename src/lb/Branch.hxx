// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "Goto.hxx"
#include "GotoConfig.hxx"

#include <list>

class LbGotoMap;
struct LbGotoIfConfig;
struct LbBranchConfig;

class LbGotoIf {
	const LbGotoIfConfig &config;

	const LbGoto destination;

public:
	LbGotoIf(LbGotoMap &goto_map, const LbGotoIfConfig &_config);

	const LbGotoIfConfig &GetConfig() const noexcept {
		return config;
	}

	template<typename C, typename R>
	[[gnu::pure]]
	bool MatchRequest(const C &connection, const R &request) const noexcept {
		return config.condition.MatchRequest(connection, request);
	}

	const LbGoto &GetDestination() const noexcept {
		return destination;
	}
};

class LbBranch {
	const LbBranchConfig &config;

	LbGoto fallback;

	std::list<LbGotoIf> conditions;

public:
	LbBranch(LbGotoMap &goto_map, const LbBranchConfig &_config);

	const LbBranchConfig &GetConfig() const noexcept {
		return config;
	}

	template<typename C, typename R>
	[[gnu::pure]]
	const LbGoto &FindRequestLeaf(const C &connection, const R &request) const noexcept {
		for (const auto &i : conditions)
			if (i.MatchRequest(connection, request))
				return i.GetDestination().FindRequestLeaf(connection, request);

		return fallback.FindRequestLeaf(connection, request);
	}
};
