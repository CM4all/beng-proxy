// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Branch.hxx"
#include "GotoConfig.hxx"
#include "GotoMap.hxx"

LbGotoIf::LbGotoIf(LbGotoMap &goto_map, const LbGotoIfConfig &_config)
	:config(_config),
	 destination(goto_map.GetInstance(config.destination))
{
}

LbBranch::LbBranch(LbGotoMap &goto_map,
		   const LbBranchConfig &_config)
	:config(_config),
	 fallback(goto_map.GetInstance(config.fallback))
{
	for (const auto &i : config.conditions)
		conditions.emplace_back(goto_map, i);
}
