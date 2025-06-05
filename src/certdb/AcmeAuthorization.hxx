// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <forward_list>
#include <string>
#include <string_view>

struct AcmeChallenge;

struct AcmeAuthorization {
	enum class Status {
		PENDING,
		VALID,
		INVALID,
		DEACTIVATED,
		EXPIRED,
		REVOKED,
	} status = Status::INVALID;

	std::string identifier;
	std::forward_list<AcmeChallenge> challenges;

	bool wildcard;

	[[gnu::pure]]
	const AcmeChallenge *FindChallengeByType(const char *type) const noexcept;

	static Status ParseStatus(std::string_view s);
	static const char *FormatStatus(Status s) noexcept;
};
