// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <utility>

struct CertDatabaseConfig;
struct UniqueCertKey;

namespace Pg { class AsyncConnection; }
namespace Co { template<typename T> class Task; }

Co::Task<UniqueCertKey>
CoGetServerCertificateKey(Pg::AsyncConnection &connection,
			  const CertDatabaseConfig &config,
			  const char *name, const char *special);
