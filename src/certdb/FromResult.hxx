/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef CERT_DATABASE_FROM_RESULT_HXX
#define CERT_DATABASE_FROM_RESULT_HXX

#include "ssl/Unique.hxx"

#include <utility>

struct CertDatabaseConfig;
class PgResult;

UniqueX509
LoadCertificate(const PgResult &result, unsigned row, unsigned column);

UniqueEVP_PKEY
LoadWrappedKey(const CertDatabaseConfig &config,
               const PgResult &result, unsigned row, unsigned column);

std::pair<UniqueX509, UniqueEVP_PKEY>
LoadCertificateKey(const CertDatabaseConfig &config,
                   const PgResult &result, unsigned row, unsigned column);

#endif
