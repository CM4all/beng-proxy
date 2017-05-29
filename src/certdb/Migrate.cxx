/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "CertDatabase.hxx"
#include "FromResult.hxx"
#include "Config.hxx"
#include "pg/CheckError.hxx"
#include "ssl/Name.hxx"
#include "util/ByteOrder.hxx"
#include "util/PrintException.hxx"

static bool
ColumnExists(Pg::Connection &c, const char *schema,
             const char *table_name, const char *column_name)
{
    if (schema == nullptr || *schema == 0)
        schema = "public";

    return CheckError(c.ExecuteParams("SELECT data_type FROM INFORMATION_SCHEMA.COLUMNS "
                                      "WHERE table_schema=$1 AND table_name=$2 AND column_name=$3",
                                      schema, table_name, column_name)).GetRowCount() > 0;

}

static bool
IndexExists(Pg::Connection &c, const char *schema,
            const char *table_name, const char *index_name)
{
    if (schema == nullptr || *schema == 0)
        schema = "public";

    return CheckError(c.ExecuteParams("SELECT indexdef FROM pg_indexes "
                                      "WHERE schemaname=$1 AND tablename=$2 AND indexname=$3",
                                      schema, table_name, index_name)).GetRowCount() > 0;
}

static void
FillIssuerCommonName(Pg::Connection &c)
{
    auto result = CheckError(c.ExecuteParams(true,
                                             "SELECT id::int8, certificate_der FROM server_certificate "
                                             "WHERE NOT deleted AND issuer_common_name IS NULL"));
    for (unsigned row = 0, n_rows = result.GetRowCount(); row < n_rows; ++row) {
        const int64_t id = FromBE64(*(const int64_t *)result.GetValue(row, 0));

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

        auto r = CheckError(c.ExecuteParams("UPDATE server_certificate "
                                            "SET issuer_common_name=$2 "
                                            "WHERE id=$1 AND NOT deleted AND issuer_common_name IS NULL",
                                            id, issuer_common_name.c_str()));
        if (r.GetAffectedRows() < 1)
            fprintf(stderr, "Certificate '%" PRId64 "' disappeared\n", id);
    }
}

void
CertDatabase::Migrate()
{
    /* server_certificate.issuer_common_name added in version 12.0.14 */

    if (!ColumnExists(conn, config.schema.c_str(), "server_certificate",
                      "issuer_common_name"))
        CheckError(conn.Execute("ALTER TABLE server_certificate "
                                "ADD COLUMN issuer_common_name varchar(256) NULL"));

    FillIssuerCommonName(conn);

    /* server_certificate.handle added in version 12.0.15 */

    if (!ColumnExists(conn, config.schema.c_str(), "server_certificate",
                      "handle"))
        CheckError(conn.Execute("ALTER TABLE server_certificate "
                                "ADD COLUMN handle varchar(256) NULL"));


    if (!IndexExists(conn, config.schema.c_str(), "server_certificate",
                     "server_certificate_handle"))
        CheckError(conn.Execute("CREATE UNIQUE INDEX server_certificate_handle "
                                "ON server_certificate(handle) "
                                "WHERE NOT deleted;"));
}
