// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "openssl/ossl_typ.h"

#include <set>
#include <string>

struct AcmeConfig;
struct AcmeChallenge;

class Dns01ChallengeRecord {
	const AcmeConfig &config;
	const std::string host;

	std::set<std::string> values;

	bool must_clear = false;

public:
	Dns01ChallengeRecord(const AcmeConfig &_config,
			     const std::string &_host) noexcept
		:config(_config), host(_host) {}

	~Dns01ChallengeRecord() noexcept;

	Dns01ChallengeRecord(const Dns01ChallengeRecord &) = delete;
	Dns01ChallengeRecord &operator=(const Dns01ChallengeRecord &) = delete;

	void AddChallenge(const AcmeChallenge &challenge,
			  const EVP_PKEY &account_key);

	void Commit();
};
