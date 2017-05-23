/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "FromResult.hxx"
#include "WrapKey.hxx"
#include "pg/Result.hxx"
#include "ssl/Certificate.hxx"
#include "ssl/Key.hxx"

UniqueX509
LoadCertificate(const PgResult &result, unsigned row, unsigned column)
{
    if (!result.IsColumnBinary(column) || result.IsValueNull(row, column))
        throw std::runtime_error("Unexpected result");

    const auto cert_der = result.GetBinaryValue(row, column);
    return DecodeDerCertificate(cert_der);
}

UniqueEVP_PKEY
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

std::pair<UniqueX509, UniqueEVP_PKEY>
LoadCertificateKey(const CertDatabaseConfig &config,
                   const PgResult &result, unsigned row, unsigned column)
{
    auto pair = std::make_pair(LoadCertificate(result, row, column),
                               LoadWrappedKey(config, result, row, column + 1));

    if (!MatchModulus(*pair.first, *pair.second))
        throw std::runtime_error("Key does not match certificate");

    return pair;
}
