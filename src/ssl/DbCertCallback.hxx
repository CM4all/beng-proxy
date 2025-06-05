// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "CertCallback.hxx"

class CertCache;

class DbSslCertCallback final : public SslCertCallback {
	CertCache &cache;

public:
	explicit DbSslCertCallback(CertCache &_cache) noexcept
		:cache(_cache) {}

	LookupCertResult OnCertCallback(SSL &ssl,
					const char *name) noexcept override;
};
