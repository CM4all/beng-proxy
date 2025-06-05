// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "PerHttpStatusCounters.hxx"

#include <map>
#include <string>

struct PerGeneratorStats {
	PerHttpStatusCounters n_per_status{};

	void AddRequest(HttpStatus status) noexcept {
		++n_per_status[HttpStatusToIndex(status)];
	}
};

struct PerGeneratorStatsMap {
	std::map<std::string, PerGeneratorStats, std::less<>> per_generator;

	void AddRequest(std::string_view generator,
			HttpStatus status) noexcept {
		auto &s = FindOrEmplace(generator);
		s.AddRequest(status);
	}

private:
	[[gnu::pure]]
	PerGeneratorStats &FindOrEmplace(std::string_view generator) noexcept {
		if (auto i = per_generator.find(generator); i != per_generator.end())
			return i->second;

		return per_generator.try_emplace(std::string{generator}).first->second;
	}
};
