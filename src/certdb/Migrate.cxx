// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "CertDatabase.hxx"
#include "FromResult.hxx"
#include "Config.hxx"
#include "pg/Reflection.hxx"
#include "lib/fmt/ExceptionFormatter.hxx"
#include "lib/openssl/Name.hxx"
#include "util/AllocatedString.hxx"
#include "util/ByteOrder.hxx"

#include <fmt/format.h>

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
		} catch (...) {
			fmt::print(stderr, "Failed to load certificate '{}': {}\n",
				   id, std::current_exception());
			continue;
		}

		auto issuer_common_name = GetIssuerCommonName(*cert);
		if (issuer_common_name == nullptr)
			continue;

		auto r = c.ExecuteParams("UPDATE server_certificate "
					 "SET issuer_common_name=$2 "
					 "WHERE id=$1 AND NOT deleted AND issuer_common_name IS NULL",
					 id, issuer_common_name.c_str());
		if (r.GetAffectedRows() < 1)
			fmt::print(stderr, "Certificate '{}' disappeared\n", id);
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

	/* server_certificate.special added in version 17.0.79 */

	conn.Execute("ALTER TABLE server_certificate ADD COLUMN IF NOT EXISTS special varchar(64) NULL");
	conn.Execute("DROP INDEX IF EXISTS server_certificate_name");
	conn.Execute("CREATE UNIQUE INDEX IF NOT EXISTS server_certificate_name_special ON server_certificate(common_name, special)");

	/* new index for faster "ON DELETE CASCADE" added in version 17.0.85 */

	conn.Execute("CREATE INDEX IF NOT EXISTS server_certificate_alt_name_owner ON server_certificate_alt_name(server_certificate_id)");

	/* time zones added in version 22.3 */

	if (Pg::GetColumnType(conn, schema, "server_certificate",
			      "not_after") == "timestamp without time zone")
		/* not_before and not_after were inserted as
		   ASN1_TIME_print() strings ("... GMT"), and the
		   column type dropped the zone, so they hold UTC;
		   "modified" is read in this session's time zone */
		conn.Execute("ALTER TABLE server_certificate"
			     " ALTER COLUMN not_before TYPE timestamp with time zone"
			     "  USING not_before AT TIME ZONE 'UTC',"
			     " ALTER COLUMN not_after TYPE timestamp with time zone"
			     "  USING not_after AT TIME ZONE 'UTC',"
			     " ALTER COLUMN modified TYPE timestamp with time zone");

	if (Pg::TableExists(conn, schema, "acme_account") &&
	    Pg::GetColumnType(conn, schema, "acme_account",
			      "time_created") == "timestamp without time zone")
		conn.Execute("ALTER TABLE acme_account"
			     " ALTER COLUMN time_created TYPE timestamp with time zone,"
			     " ALTER COLUMN time_used TYPE timestamp with time zone,"
			     " ALTER COLUMN time_rejected TYPE timestamp with time zone");
}
