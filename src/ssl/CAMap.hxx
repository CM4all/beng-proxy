// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "lib/openssl/Hash.hxx"
#include "lib/openssl/UniqueX509.hxx"

#include <forward_list>
#include <map>

#include <string.h>

class CertDatabase;

/**
 * A map of CA certificate chains.
 */
class CAMap final {
	struct SHA1Compare {
		[[gnu::pure]]
		bool operator()(const SHA1Digest &a,
				const SHA1Digest &b) const noexcept {
			return memcmp(&a, &b, sizeof(a)) < 0;
		}
	};

	using Chain = std::forward_list<UniqueX509>;

	std::map<SHA1Digest, Chain, SHA1Compare> map;

public:
	void LoadChainFile(const char *path);

	[[gnu::pure]]
	const Chain *Find(const X509_NAME &subject) const noexcept;

	[[gnu::pure]]
	const Chain *FindIssuer(const X509 &cert) const noexcept;
};
