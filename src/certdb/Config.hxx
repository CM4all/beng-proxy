// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

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

	/**
	 * Throws on error.
	 */
	const WrapKey &GetWrapKey(std::string_view name) const;

	/**
	 * Throws on error.
	 */
	std::pair<const char *, const WrapKey *> GetDefaultWrapKey() const;
};

CertDatabaseConfig
LoadStandaloneCertDatabaseConfig(const char *path);
