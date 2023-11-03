// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "openssl/ossl_typ.h"

#include <string>

struct AcmeChallenge;

/**
 * @return file contents
 */
std::string
MakeHttp01(const AcmeChallenge &challenge, const EVP_PKEY &account_key);

class Http01ChallengeFile final {
	std::string path;

public:
	Http01ChallengeFile(const std::string &directory,
			    const AcmeChallenge &challenge,
			    const EVP_PKEY &account_key);

	~Http01ChallengeFile() noexcept;

	Http01ChallengeFile(const Http01ChallengeFile &) = delete;
	Http01ChallengeFile &operator=(const Http01ChallengeFile &) = delete;
};
