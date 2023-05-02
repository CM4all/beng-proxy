// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "lib/openssl/UniqueX509.hxx"

#include <string>

class CertDatabase;
struct CertDatabaseConfig;
struct AcmeChallenge;

class Alpn01ChallengeRecord {
	CertDatabase &db;
	const std::string host, handle;

	const UniqueX509 cert;

public:
	Alpn01ChallengeRecord(CertDatabase &_db,
			      std::string_view _host);

	~Alpn01ChallengeRecord() noexcept;

	Alpn01ChallengeRecord(const Alpn01ChallengeRecord &) = delete;
	Alpn01ChallengeRecord &operator=(const Alpn01ChallengeRecord &) = delete;

	void AddChallenge(const AcmeChallenge &challenge,
			  EVP_PKEY &account_key);

	void Commit(const CertDatabaseConfig &db_config);
};
