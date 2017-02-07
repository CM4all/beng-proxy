/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "CertDatabase.hxx"
#include "Config.hxx"
#include "WrapKey.hxx"
#include "pg/CheckError.hxx"
#include "ssl/Buffer.hxx"
#include "ssl/Time.hxx"
#include "ssl/Name.hxx"
#include "ssl/AltName.hxx"
#include "ssl/Certificate.hxx"
#include "ssl/Key.hxx"

#include <openssl/aes.h>

#include <sys/poll.h>

CertDatabase::CertDatabase(const CertDatabaseConfig &_config)
    :config(_config), conn(config.connect.c_str())
{
    if (!config.schema.empty() && !conn.SetSchema(config.schema.c_str()))
        throw std::runtime_error("Failed to set schema '" + config.schema + ": " +
                                 conn.GetErrorMessage());
}

bool
CertDatabase::CheckConnected()
{
    if (GetStatus() != CONNECTION_OK)
        return false;

    struct pollfd pfd = {
        .fd = GetSocket(),
        .events = POLLIN,
    };

    if (poll(&pfd, 1, 0) == 0)
        return true;

    conn.ConsumeInput();
    if (GetStatus() != CONNECTION_OK)
        return false;

    /* try again, just in case the previous PQconsumeInput() call has
       read a final message from the socket */

    if (poll(&pfd, 1, 0) == 0)
        return true;

    conn.ConsumeInput();
    return GetStatus() == CONNECTION_OK;
}

void
CertDatabase::EnsureConnected()
{
    if (!CheckConnected())
        conn.Reconnect();
}

PgResult
CertDatabase::ListenModified()
{
    std::string sql("LISTEN \"");
    if (!config.schema.empty() && config.schema != "public") {
        /* prefix the notify name unless we're in the default
           schema */
        sql += config.schema;
        sql += ':';
    }

    sql += "modified\"";

    return conn.Execute(sql.c_str());
}

PgResult
CertDatabase::NotifyModified()
{
    std::string sql("NOTIFY \"");
    if (!config.schema.empty() && config.schema != "public") {
        /* prefix the notify name unless we're in the default
           schema */
        sql += config.schema;
        sql += ':';
    }

    sql += "modified\"";

    return conn.Execute(sql.c_str());
}

void
CertDatabase::InsertServerCertificate(const char *common_name,
                                      const char *not_before,
                                      const char *not_after,
                                      X509 &cert, ConstBuffer<void> key,
                                      const char *key_wrap_name)
{
    const SslBuffer cert_buffer(cert);
    const PgBinaryValue cert_der(cert_buffer.get());

    const PgBinaryValue key_der(key);

    CheckError(InsertServerCertificate(common_name,
                                       not_before, not_after,
                                       cert_der, key_der, key_wrap_name));
}

bool
CertDatabase::LoadServerCertificate(X509 &cert, EVP_PKEY &key,
                                    const char *key_wrap_name,
                                    AES_KEY *wrap_key)
{
    const auto common_name = GetCommonName(cert);
    assert(common_name != nullptr);

    const SslBuffer cert_buffer(cert);
    const PgBinaryValue cert_der(cert_buffer.get());

    const SslBuffer key_buffer(key);
    PgBinaryValue key_der(key_buffer.get());

    std::unique_ptr<unsigned char[]> wrapped;

    if (wrap_key != nullptr) {
        /* encrypt the private key */

        std::unique_ptr<unsigned char[]> padded;
        size_t padded_size = ((key_der.size - 1) | 7) + 1;
        if (padded_size != key_der.size) {
            /* pad with zeroes */
            padded.reset(new unsigned char[padded_size]);

            unsigned char *p = padded.get();
            p = std::copy_n((const unsigned char *)key_der.data,
                            key_der.size, p);
            std::fill(p, padded.get() + padded_size, 0);

            key_der.data = padded.get();
            key_der.size = padded_size;
        }

        wrapped.reset(new unsigned char[key_der.size + 8]);
        int result = AES_wrap_key(wrap_key, nullptr,
                                  wrapped.get(),
                                  (const unsigned char *)key_der.data,
                                  key_der.size);
        if (result <= 0)
            throw SslError("AES_wrap_key() failed");

        key_der.data = wrapped.get();
        key_der.size = result;
    }

    const auto alt_names = GetSubjectAltNames(cert);

    const auto not_before = FormatTime(X509_get_notBefore(&cert));
    if (not_before == nullptr)
        throw "Certificate does not have a notBefore time stamp";

    const auto not_after = FormatTime(X509_get_notAfter(&cert));
    if (not_after == nullptr)
        throw "Certificate does not have a notAfter time stamp";

    auto result = CheckError(UpdateServerCertificate(common_name.c_str(),
                                                     not_before.c_str(),
                                                     not_after.c_str(),
                                                     cert_der, key_der,
                                                     key_wrap_name));
    if (result.GetRowCount() > 0) {
        const char *id = result.GetValue(0, 0);
        DeleteAltNames(id);
        for (const auto &alt_name : alt_names)
            CheckError(InsertAltName(id, alt_name.c_str()));
        return false;
    } else {
        result = CheckError(InsertServerCertificate(common_name.c_str(),
                                                    not_before.c_str(),
                                                    not_after.c_str(),
                                                    cert_der, key_der,
                                                    key_wrap_name));
        const char *id = result.GetValue(0, 0);
        for (const auto &alt_name : alt_names)
            CheckError(InsertAltName(id, alt_name.c_str()));
        return true;
    }
}

unsigned
CertDatabase::DeleteAcmeInvalidAlt(X509 &cert)
{
    return CheckError(DeleteAcmeInvalidByNames(GetSubjectAltNames(cert))).GetAffectedRows();
}

static UniqueX509
LoadCertificate(const PgResult &result, unsigned row, unsigned column)
{
    if (!result.IsColumnBinary(column) || result.IsValueNull(row, column))
        throw std::runtime_error("Unexpected result");

    const auto cert_der = result.GetBinaryValue(row, column);
    return DecodeDerCertificate(cert_der);
}

static UniqueEVP_PKEY
LoadWrappedKey(const CertDatabaseConfig &config,
               const PgResult &result, unsigned row, unsigned column)
{
    if (!result.IsColumnBinary(column) || result.IsValueNull(row, column))
        throw std::runtime_error("Unexpected result");

    auto key_der = result.GetBinaryValue(row, column);

    std::unique_ptr<unsigned char[]> unwrapped;
    if (!result.IsValueNull(row, column + 1)) {
        /* the private key is encrypted; descrypt it using the AES key
           from the configuration file */
        const auto key_wrap_name = result.GetValue(row, column + 1);
        key_der = UnwrapKey(key_der, config, key_wrap_name, unwrapped);
    }

    return DecodeDerKey(key_der);
}

static std::pair<UniqueX509, UniqueEVP_PKEY>
LoadCertificateKey(const CertDatabaseConfig &config,
                   const PgResult &result, unsigned row, unsigned column)
{
    auto pair = std::make_pair(LoadCertificate(result, row, column),
                               LoadWrappedKey(config, result, row, column + 1));

    if (!MatchModulus(*pair.first, *pair.second))
        throw std::runtime_error("Key does not match certificate");

    return pair;
}

UniqueX509
CertDatabase::GetServerCertificate(const char *name)
{
    auto result = CheckError(FindServerCertificateByName(name));
    if (result.GetRowCount() == 0)
        return nullptr;

    return LoadCertificate(result, 0, 0);
}

std::pair<UniqueX509, UniqueEVP_PKEY>
CertDatabase::GetServerCertificateKey(const char *name)
{
    auto result = CheckError(FindServerCertificateKeyByName(name));
    if (result.GetRowCount() == 0)
        return std::make_pair(nullptr, nullptr);

    return LoadCertificateKey(config, result, 0, 0);
}
