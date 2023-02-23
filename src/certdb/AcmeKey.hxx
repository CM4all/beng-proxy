// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "lib/openssl/UniqueEVP.hxx"

/**
 * An ACME account key.
 */
class AcmeKey {
	UniqueEVP_PKEY key;

public:
	explicit AcmeKey(UniqueEVP_PKEY &&_key) noexcept
		:key(std::move(_key)) {}

	explicit AcmeKey(const char *path);

	AcmeKey(AcmeKey &&) = default;
	AcmeKey &operator=(AcmeKey &&) = default;

	auto &&operator*() const noexcept {
		return *key;
	}
};
