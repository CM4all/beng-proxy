// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <string>

struct AcmeConfig {
	std::string account_key_path = "/etc/cm4all/acme/account.key";
	std::string account_key_id;

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

	bool fake = false;
};
