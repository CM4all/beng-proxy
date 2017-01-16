/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "CertDatabase.hxx"
#include "Config.hxx"
#include "pg/CheckError.hxx"
#include "ssl/MemBio.hxx"
#include "ssl/Buffer.hxx"
#include "ssl/Name.hxx"
#include "ssl/AltName.hxx"

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

gcc_pure
static AllocatedString<>
FormatTime(ASN1_TIME &t)
{
    return BioWriterToString([&t](BIO &bio){
            ASN1_TIME_print(&bio, &t);
        });
}

gcc_pure
static AllocatedString<>
FormatTime(ASN1_TIME *t)
{
    if (t == nullptr)
        return nullptr;

    return FormatTime(*t);
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

UniqueX509
CertDatabase::GetServerCertificate(const char *name)
{
    auto result = CheckError(FindServerCertificateByName(name));
    if (result.GetRowCount() == 0)
        return nullptr;

    if (!result.IsColumnBinary(0) || result.IsValueNull(0, 0))
        throw "Unexpected result";

    auto cert_der = result.GetBinaryValue(0, 0);

    auto data = (const unsigned char *)cert_der.data;
    UniqueX509 cert(d2i_X509(nullptr, &data, cert_der.size));
    if (!cert)
        throw "d2i_X509() failed";

    return cert;
}
