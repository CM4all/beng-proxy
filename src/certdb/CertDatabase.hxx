/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef CERT_DATABASE_HXX
#define CERT_DATABASE_HXX

#include "pg/Connection.hxx"
#include "ssl/Unique.hxx"

#include <string>

typedef struct aes_key_st AES_KEY;
struct CertDatabaseConfig;

class CertDatabase {
    const CertDatabaseConfig &config;

    PgConnection conn;

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
        const auto result = conn.Execute("SELECT MAX(modified) FROM server_certificate");
        return result.GetOnlyStringChecked();
    }

    bool BeginSerializable() {
        return conn.BeginSerializable();
    }

    bool Commit() {
        return conn.Commit();
    }

    void Migrate();

    void InsertServerCertificate(const char *common_name,
                                 const char *issuer_common_name,
                                 const char *not_before,
                                 const char *not_after,
                                 X509 &cert, ConstBuffer<void> key,
                                 const char *key_wrap_name);

    /**
     * @return true when new certificate has been inserted, false when an
     * existing certificate has been updated
     */
    bool LoadServerCertificate(X509 &cert, EVP_PKEY &key,
                               const char *key_wrap_name,
                               AES_KEY *wrap_key);

    /**
     * Delete *.acme.invalid for alt_names of the given certificate.
     */
    unsigned DeleteAcmeInvalidAlt(X509 &cert);

    UniqueX509 GetServerCertificate(const char *name);

    /**
     * Throws std:;runtime_error on error.
     *
     * @return a pair of certificate and key, or {nullptr, nullptr} if
     * no matching certificate was found
     */
    std::pair<UniqueX509, UniqueEVP_PKEY> GetServerCertificateKey(const char *name);
    std::pair<UniqueX509, UniqueEVP_PKEY> GetServerCertificateKey(unsigned id);

private:
    PgResult InsertServerCertificate(const char *common_name,
                                     const char *issuer_common_name,
                                     const char *not_before,
                                     const char *not_after,
                                     PgBinaryValue cert, PgBinaryValue key,
                                     const char *key_wrap_name) {
        return conn.ExecuteBinary("INSERT INTO server_certificate("
                                  "common_name, issuer_common_name, "
                                  "not_before, not_after, "
                                  "certificate_der, key_der, key_wrap_name) "
                                  "VALUES($1, $2, $3, $4, $5, $6)"
                                  " RETURNING id",
                                  common_name, issuer_common_name,
                                  not_before, not_after,
                                  cert, key, key_wrap_name);
    }

    PgResult UpdateServerCertificate(const char *common_name,
                                     const char *issuer_common_name,
                                     const char *not_before,
                                     const char *not_after,
                                     PgBinaryValue cert, PgBinaryValue key,
                                     const char *key_wrap_name) {
        return conn.ExecuteBinary("UPDATE server_certificate SET "
                                  "not_before=$2, not_after=$3, "
                                  "certificate_der=$4, key_der=$5, "
                                  "key_wrap_name=$6, "
                                  "issuer_common_name=$7, "
                                  "modified=CURRENT_TIMESTAMP, deleted=FALSE "
                                  "WHERE common_name=$1"
                                  " RETURNING id",
                                  common_name, not_before, not_after,
                                  cert, key, key_wrap_name,
                                  issuer_common_name);
    }

    PgResult DeleteAltNames(const char *server_certificate_id) {
        return conn.ExecuteParams("DELETE FROM server_certificate_alt_name"
                                  " WHERE server_certificate_id=$1",
                                  server_certificate_id);
    }

    PgResult InsertAltName(const char *server_certificate_id,
                           const char *name) {
        return conn.ExecuteParams("INSERT INTO server_certificate_alt_name"
                                  "(server_certificate_id, name)"
                                  " VALUES($1, $2)",
                                  server_certificate_id, name);
    }

public:
    PgResult DeleteServerCertificateByName(const char *common_name) {
        return conn.ExecuteParams(true,
                                  "UPDATE server_certificate SET "
                                  "modified=CURRENT_TIMESTAMP, deleted=TRUE "
                                  "WHERE common_name=$1 AND NOT deleted",
                                  common_name);
    }

private:
    template<typename T>
    PgResult DeleteAcmeInvalidByNames(const T &names) {
        return conn.ExecuteParams(true,
                                  "UPDATE server_certificate SET "
                                  "modified=CURRENT_TIMESTAMP, deleted=TRUE "
                                  "WHERE NOT deleted AND common_name = ANY($1)"
                                  " AND EXISTS("
                                  "SELECT id FROM server_certificate_alt_name"
                                  " WHERE server_certificate_id=server_certificate.id"
                                  " AND name LIKE '%.acme.invalid')",
                                  names);
    }

    PgResult FindServerCertificateByName(const char *common_name) {
        return conn.ExecuteParams(true,
                                  "SELECT certificate_der "
                                  "FROM server_certificate "
                                  "WHERE NOT deleted AND "
                                  "(common_name=$1 OR EXISTS("
                                  "SELECT id FROM server_certificate_alt_name"
                                  " WHERE server_certificate_id=server_certificate.id"
                                  " AND name=$1))"
                                  "ORDER BY"
                                  /* prefer certificates which expire later */
                                  " not_after DESC,"
                                  /* prefer exact match in common_name: */
                                  " common_name=$1 DESC "
                                  "LIMIT 1",
                                  common_name);
    }

    PgResult FindServerCertificateKeyByName(const char *common_name) {
        return conn.ExecuteParams(true,
                                  "SELECT certificate_der, key_der, key_wrap_name "
                                  "FROM server_certificate "
                                  "WHERE NOT deleted AND "
                                  "(common_name=$1 OR EXISTS("
                                  "SELECT id FROM server_certificate_alt_name"
                                  " WHERE server_certificate_id=server_certificate.id"
                                  " AND name=$1))"
                                  "ORDER BY"
                                  /* prefer certificates which expire later */
                                  " not_after DESC,"
                                  /* prefer exact match in common_name: */
                                  " common_name=$1 DESC "
                                  "LIMIT 1",
                                  common_name);
    }

    PgResult FindServerCertificateKeyById(unsigned id) {
        return conn.ExecuteParams(true,
                                  "SELECT certificate_der, key_der, key_wrap_name "
                                  "FROM server_certificate "
                                  "WHERE id=$1",
                                  id);
    }

public:
    PgResult GetModifiedServerCertificatesMeta(const char *since) {
        return conn.ExecuteParams("SELECT deleted, modified, common_name "
                                  "FROM server_certificate "
                                  "WHERE modified>$1",
                                  since);
    }

    PgResult TailModifiedServerCertificatesMeta() {
        return conn.Execute("SELECT deleted, modified, common_name "
                            "FROM server_certificate "
                            "ORDER BY modified LIMIT 20");
    }
};

#endif
