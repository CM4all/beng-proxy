/*
 * Copyright 2007-2020 CM4all GmbH
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

#include "CertDatabase.hxx"
#include "FromResult.hxx"
#include "Config.hxx"
#include "pg/Reflection.hxx"
#include "ssl/Name.hxx"
#include "util/ByteOrder.hxx"
#include "util/PrintException.hxx"

static void
FillIssuerCommonName(Pg::Connection &c)
{
	auto result = c.Execute(true,
				"SELECT id::int8, certificate_der FROM server_certificate "
				"WHERE NOT deleted AND issuer_common_name IS NULL");
	for (unsigned row = 0, n_rows = result.GetRowCount(); row < n_rows; ++row) {
		const int64_t id = FromBE64(*(const int64_t *)(const void *)result.GetValue(row, 0));

		UniqueX509 cert;
		try {
			cert = LoadCertificate(result, row, 1);
		} catch (const std::runtime_error &e) {
			fprintf(stderr, "Failed to load certificate '%" PRId64 "'\n", id);
			PrintException(e);
			continue;
		}

		auto issuer_common_name = GetIssuerCommonName(*cert);
		if (issuer_common_name.IsNull())
			continue;

		auto r = c.ExecuteParams("UPDATE server_certificate "
					 "SET issuer_common_name=$2 "
					 "WHERE id=$1 AND NOT deleted AND issuer_common_name IS NULL",
					 id, issuer_common_name.c_str());
		if (r.GetAffectedRows() < 1)
			fprintf(stderr, "Certificate '%" PRId64 "' disappeared\n", id);
	}
}

void
CertDatabase::Migrate()
{
	const char *schema = config.schema.empty()
		? "public"
		: config.schema.c_str();

	/* server_certificate.issuer_common_name added in version 12.0.14 */

	if (!Pg::ColumnExists(conn, schema, "server_certificate",
			      "issuer_common_name"))
		conn.Execute("ALTER TABLE server_certificate "
			     "ADD COLUMN issuer_common_name varchar(256) NULL");

	FillIssuerCommonName(conn);

	/* server_certificate.handle added in version 12.0.15 */

	if (!Pg::ColumnExists(conn, schema, "server_certificate",
			      "handle"))
		conn.Execute("ALTER TABLE server_certificate "
			     "ADD COLUMN handle varchar(256) NULL");


	if (!Pg::IndexExists(conn, schema, "server_certificate",
			     "server_certificate_handle"))
		conn.Execute("CREATE UNIQUE INDEX server_certificate_handle "
			     "ON server_certificate(handle);");
}
