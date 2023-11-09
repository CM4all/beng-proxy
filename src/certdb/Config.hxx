// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "WrapKey.hxx"

#include <map>
#include <string>

class LineParser;

struct CertDatabaseConfig {
	std::string connect;
	std::string schema;

	std::map<std::string, WrapKey, std::less<>> wrap_keys;

	std::string default_wrap_key;

	/**
	 * Throws on error.
	 *
	 * @return false if the word was not recognized
	 */
	bool ParseLine(const char *word, LineParser &line);

	/**
	 * Throws on error.
	 */
	void Check();
};

CertDatabaseConfig
LoadStandaloneCertDatabaseConfig(const char *path);
