// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <string>

struct AcmeConfig {
	std::string account_key_path = "/etc/cm4all/acme/account.key";
	std::string account_key_id;

	std::string tls_ca;

	std::string directory_url;

	/**
	 * Specifies the directory mapped to
	 * "http://example.com/.well-known/acme-challenge/".
	 */
	std::string challenge_directory;

	std::string dns_txt_program;

	bool alpn = false;

	bool account_db = false;

	bool debug = false;

	bool staging = false;

	const char *GetDirectoryURL() const noexcept {
		if (!directory_url.empty())
			return directory_url.c_str();

		return staging
			? "https://acme-staging-v02.api.letsencrypt.org/directory"
			: "https://acme-v02.api.letsencrypt.org/directory";
	}
};
