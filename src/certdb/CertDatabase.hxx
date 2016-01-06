/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef CERT_DATABASE_HXX
#define CERT_DATABASE_HXX

#include "pg/Connection.hxx"

#include <string>
#include <vector>

struct CertDatabaseConfig;

class CertDatabase {
    PgConnection conn;

    const std::string schema;

public:
    explicit CertDatabase(const CertDatabaseConfig &_config);

    ConnStatusType GetStatus() const {
        return conn.GetStatus();
    }

    gcc_pure
    const char *GetErrorMessage() const {
        return conn.GetErrorMessage();
    }

    bool CheckConnected();
    void EnsureConnected();

    gcc_pure
    int GetSocket() const {
        return conn.GetSocket();
    }

    void ConsumeInput() {
        conn.ConsumeInput();
    }

    PgNotify GetNextNotify() {
        return conn.GetNextNotify();
    }

    PgResult ListenModified();
    PgResult NotifyModified();

    gcc_pure
    std::string GetCurrentTimestamp() {
        const auto result = conn.Execute("SELECT CURRENT_TIMESTAMP");
        return result.GetOnlyStringChecked();
    }

    gcc_pure
    std::string GetLastModified() {
        const auto result = conn.Execute("SELECT MAX(modified) FROM server_certificates");
        return result.GetOnlyStringChecked();
    }

    PgResult InsertServerCertificate(const char *common_name,
                                     const char *not_before,
                                     const char *not_after,
                                     PgBinaryValue cert, PgBinaryValue key) {
        return conn.ExecuteBinary("INSERT INTO server_certificates("
                                  "common_name, not_before, not_after, "
                                  "certificate_der, key_der) "
                                  "VALUES($1, $2, $3, $4, $5)",
                                  common_name, not_before, not_after,
                                  cert, key);
    }

    PgResult UpdateServerCertificate(const char *common_name,
                                     const char *not_before,
                                     const char *not_after,
                                     PgBinaryValue cert, PgBinaryValue key) {
        return conn.ExecuteBinary("UPDATE server_certificates SET "
                                  "not_before=$2, not_after=$3, "
                                  "certificate_der=$4, key_der=$5, "
                                  "modified=CURRENT_TIMESTAMP, deleted=FALSE "
                                  "WHERE common_name=$1",
                                  common_name, not_before, not_after,
                                  cert, key);
    }

    PgResult DeleteServerCertificateByCommonName(const char *common_name) {
        return conn.ExecuteParams(true,
                                  "DELETE FROM server_certificates "
                                  "WHERE common_name=$1 AND NOT deleted",
                                  common_name);
    }

    PgResult FindServerCertificateByCommonName(const char *common_name) {
        return conn.ExecuteParams(true,
                                  "SELECT certificate_der "
                                  "FROM server_certificates "
                                  "WHERE common_name=$1 AND NOT deleted",
                                  common_name);
    }

    PgResult FindServerCertificateKeyByCommonName(const char *common_name) {
        return conn.ExecuteParams(true,
                                  "SELECT certificate_der, key_der "
                                  "FROM server_certificates "
                                  "WHERE common_name=$1 AND NOT deleted",
                                  common_name);
    }

    PgResult GetModifiedServerCertificatesMeta(const char *since) {
        return conn.ExecuteParams("SELECT deleted, modified, common_name "
                                  "FROM server_certificates "
                                  "WHERE modified>$1",
                                  since);
    }

    PgResult TailModifiedServerCertificatesMeta() {
        return conn.ExecuteParams("SELECT deleted, modified, common_name "
                                  "FROM server_certificates "
                                  "ORDER BY modified LIMIT 20");
    }
};

#endif
