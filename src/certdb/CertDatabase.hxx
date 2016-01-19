/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef CERT_DATABASE_HXX
#define CERT_DATABASE_HXX

#include "pg/Connection.hxx"
#include "ssl/Unique.hxx"

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

    bool BeginSerializable() {
        return conn.BeginSerializable();
    }

    bool Commit() {
        return conn.Commit();
    }

    void InsertServerCertificate(const char *common_name,
                                 const char *not_before,
                                 const char *not_after,
                                 X509 &cert, ConstBuffer<void> key);

    /**
     * @return true when new certificate has been inserted, false when an
     * existing certificate has been updated
     */
    bool LoadServerCertificate(X509 &cert, EVP_PKEY &key);

    /**
     * Delete *.acme.invalid for alt_names of the given certificate.
     */
    unsigned DeleteAcmeInvalidAlt(X509 &cert);

    UniqueX509 GetServerCertificate(const char *name);

private:
    PgResult InsertServerCertificate(const char *common_name,
                                     const std::list<std::string> &_alt_names,
                                     const char *not_before,
                                     const char *not_after,
                                     PgBinaryValue cert, PgBinaryValue key) {
        const auto *alt_names = _alt_names.empty()
            ? nullptr
            : &_alt_names;

        return conn.ExecuteBinary("INSERT INTO server_certificates("
                                  "common_name, alt_names, "
                                  "not_before, not_after, "
                                  "certificate_der, key_der) "
                                  "VALUES($1, $2, $3, $4, $5, $6)",
                                  common_name, alt_names,
                                  not_before, not_after,
                                  cert, key);
    }

    PgResult UpdateServerCertificate(const char *common_name,
                                     const std::list<std::string> &_alt_names,
                                     const char *not_before,
                                     const char *not_after,
                                     PgBinaryValue cert, PgBinaryValue key) {
        const auto *alt_names = _alt_names.empty()
            ? nullptr
            : &_alt_names;

        return conn.ExecuteBinary("UPDATE server_certificates SET "
                                  "alt_names=$6, "
                                  "not_before=$2, not_after=$3, "
                                  "certificate_der=$4, key_der=$5, "
                                  "modified=CURRENT_TIMESTAMP, deleted=FALSE "
                                  "WHERE common_name=$1",
                                  common_name, not_before, not_after,
                                  cert, key, alt_names);
    }

public:
    PgResult DeleteServerCertificateByName(const char *common_name) {
        return conn.ExecuteParams(true,
                                  "UPDATE server_certificates SET "
                                  "modified=CURRENT_TIMESTAMP, deleted=TRUE "
                                  "WHERE common_name=$1 AND NOT deleted",
                                  common_name);
    }

private:
    PgResult DeleteAcmeInvalidByNames(const std::list<std::string> &names) {
        return conn.ExecuteParams(true,
                                  "UPDATE server_certificates SET "
                                  "modified=CURRENT_TIMESTAMP, deleted=TRUE "
                                  "WHERE id IN ("
                                  "SELECT id FROM ("
                                  "SELECT id, alt_names, generate_subscripts(alt_names, 1) AS s "
                                  "FROM server_certificates "
                                  "WHERE NOT deleted AND common_name = ANY($1)) AS t "
                                  "WHERE alt_names[s] LIKE '%.acme.invalid'"
                                  ")",
                                  &names);
    }

    PgResult FindServerCertificateByName(const char *common_name) {
        return conn.ExecuteParams(true,
                                  "SELECT certificate_der "
                                  "FROM server_certificates "
                                  "WHERE NOT deleted AND "
                                  "(common_name=$1 OR ARRAY[$1::varchar] <@ alt_names) "
                                  /* prefer exact match in common_name: */
                                  "ORDER BY common_name=$1 DESC LIMIT 1",
                                  common_name);
    }

public:
    PgResult FindServerCertificateKeyByName(const char *common_name) {
        return conn.ExecuteParams(true,
                                  "SELECT certificate_der, key_der "
                                  "FROM server_certificates "
                                  "WHERE NOT deleted AND "
                                  "(common_name=$1 OR ARRAY[$1::varchar] <@ alt_names) "
                                  /* prefer exact match in common_name: */
                                  "ORDER BY common_name=$1 DESC LIMIT 1",
                                  common_name);
    }

    PgResult GetModifiedServerCertificatesMeta(const char *since) {
        return conn.ExecuteParams("SELECT deleted, modified, common_name "
                                  "FROM server_certificates "
                                  "WHERE modified>$1",
                                  since);
    }

    PgResult TailModifiedServerCertificatesMeta() {
        return conn.Execute("SELECT deleted, modified, common_name "
                            "FROM server_certificates "
                            "ORDER BY modified LIMIT 20");
    }
};

#endif
