// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "CoCertDatabase.hxx"
#include "Queries.hxx"
#include "FromResult.hxx"
#include "lib/openssl/UniqueCertKey.hxx"
#include "pg/CoQuery.hxx"
#include "co/Task.hxx"

/**
 * A callable which invokes Pg::CoQuery().
 */
struct CoQueryWrapper {
	Pg::AsyncConnection &connection;

	template<typename... Params>
	auto operator()(const Params&... params) const {
		return Pg::CoQuery(connection,
				   Pg::CoQuery::CancelType::DISCARD,
				   params...);
	}
};

Co::Task<UniqueCertKey>
CoGetServerCertificateKey(Pg::AsyncConnection &connection,
			  const CertDatabaseConfig &config,
			  const char *name, const char *special)
{
	const CoQueryWrapper q{connection};

	auto result = co_await FindServerCertificateKeyByName(q, name, special);
	if (result.GetRowCount() == 0) {
		/* no matching common_name; check for an altName */
		// TODO do both queries, use the most recent record
		result = co_await FindServerCertificateKeyByAltName(q, name, special);
		if (result.GetRowCount() == 0)
			co_return UniqueCertKey{};
	}

	co_return LoadCertificateKey(config, result, 0, 0);
}
