// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "lib/openssl/UniqueX509.hxx"
#include "lib/openssl/UniqueEVP.hxx"

#include <utility>

struct CertDatabaseConfig;
struct UniqueCertKey;
namespace Pg { class Result; }

UniqueX509
LoadCertificate(const Pg::Result &result, unsigned row, unsigned column);

UniqueEVP_PKEY
LoadWrappedKey(const CertDatabaseConfig &config,
	       const Pg::Result &result, unsigned row, unsigned column);

UniqueCertKey
LoadCertificateKey(const CertDatabaseConfig &config,
		   const Pg::Result &result, unsigned row, unsigned column);
