/*
 * Copyright 2007-2017 Content Management AG
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

#include "FromResult.hxx"
#include "WrapKey.hxx"
#include "pg/Result.hxx"
#include "ssl/Certificate.hxx"
#include "ssl/Key.hxx"

UniqueX509
LoadCertificate(const Pg::Result &result, unsigned row, unsigned column)
{
    if (!result.IsColumnBinary(column) || result.IsValueNull(row, column))
        throw std::runtime_error("Unexpected result");

    const auto cert_der = result.GetBinaryValue(row, column);
    return DecodeDerCertificate(cert_der);
}

UniqueEVP_PKEY
LoadWrappedKey(const CertDatabaseConfig &config,
               const Pg::Result &result, unsigned row, unsigned column)
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
                   const Pg::Result &result, unsigned row, unsigned column)
{
    auto pair = std::make_pair(LoadCertificate(result, row, column),
                               LoadWrappedKey(config, result, row, column + 1));

    if (!MatchModulus(*pair.first, *pair.second))
        throw std::runtime_error("Key does not match certificate");

    return pair;
}
