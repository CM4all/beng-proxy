// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "HttpStats.hxx"

#include <map>
#include <string>

struct TaggedHttpStats {
	std::map<std::string, HttpStats, std::less<>> per_tag;

	void AddRequest(std::string_view tag,
			HttpStatus status,
			uint64_t bytes_received,
			uint64_t bytes_sent,
			std::chrono::steady_clock::duration duration) noexcept {
		auto &s = FindOrEmplace(tag);
		s.AddRequest(status, bytes_received, bytes_sent,
			     duration);
	}

private:
	HttpStats &FindOrEmplace(std::string_view tag) noexcept {
		if (auto i = per_tag.find(tag); i != per_tag.end())
			return i->second;

		return per_tag.try_emplace(std::string{tag}).first->second;
	}
};
