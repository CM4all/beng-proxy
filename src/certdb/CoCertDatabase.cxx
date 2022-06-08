/*
 * Copyright 2007-2022 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

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
