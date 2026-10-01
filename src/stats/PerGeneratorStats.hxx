// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "PerHttpStatusCounters.hxx"
#include "util/CharUtil.hxx"
#include "util/StringVerify.hxx"

#include <map>
#include <string>

constexpr bool
IsAllowedGeneratorChar(char ch) noexcept
{
	return IsAlphaNumericASCII(ch) ||
		ch == '-' || ch == '_' || ch == '.' || ch == ':' || ch == '/';
}

struct PerGeneratorStats {
	PerHttpStatusCounters n_per_status{};

	void AddRequest(HttpStatus status) noexcept {
		++n_per_status[HttpStatusToIndex(status)];
	}
};

struct PerGeneratorStatsMap {
	/**
	 * The maximum number of generators tracked by this map.  This
	 * is a safety limit because the generator name may be
	 * supplied by an untrusted backend (via the
	 * "X-CM4all-Generator" response header) and this map is never
	 * pruned.
	 */
	static constexpr std::size_t MAX_GENERATORS = 256;

	/**
	 * The maximum length of a generator name.
	 */
	static constexpr std::size_t MAX_GENERATOR_LENGTH = 64;

	std::map<std::string, PerGeneratorStats, std::less<>> per_generator;

	std::size_t n_generators = 0;

	void AddRequest(std::string_view generator,
			HttpStatus status) noexcept {
		if (auto *s = FindOrEmplace(generator))
			s->AddRequest(status);
	}

private:
	[[gnu::pure]]
	PerGeneratorStats *FindOrEmplace(std::string_view generator) noexcept {
		if (auto i = per_generator.find(generator); i != per_generator.end())
			return &i->second;

		if (generator.size() > MAX_GENERATOR_LENGTH ||
		    n_generators >= MAX_GENERATORS)
			/* too many (or too long) generator names;
			   ignore this one */
			return nullptr;

		if (!CheckChars(generator, IsAllowedGeneratorChar))
			/* this name becomes a Prometheus label value;
			   refuse everything which would need escaping
			   there */
			return nullptr;

		++n_generators;
		return &per_generator.try_emplace(std::string{generator}).first->second;
	}
};
