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

#ifndef BENG_PROXY_SSL_WRAP_KEY_HXX
#define BENG_PROXY_SSL_WRAP_KEY_HXX

#include "Config.hxx"
#include "ssl/Error.hxx"
#include "pg/BinaryValue.hxx"

#include <openssl/aes.h>

#include <memory>

class WrapKeyHelper {
    AES_KEY buffer;

public:
    AES_KEY *SetEncryptKey(const CertDatabaseConfig::AES256 &key) {
        if (AES_set_encrypt_key(key.data(), sizeof(key) * 8, &buffer) != 0)
            throw SslError("AES_set_encrypt_key() failed");

        return &buffer;
    }

    AES_KEY *SetEncryptKey(const CertDatabaseConfig &config,
                           const std::string &name) {
        const auto i = config.wrap_keys.find(name);
        if (i == config.wrap_keys.end())
            throw std::runtime_error("No such wrap_key: " + name);

        return SetEncryptKey(i->second);
    }

    std::pair<const char *,
              AES_KEY *> SetEncryptKey(const CertDatabaseConfig &config) {
        if (config.default_wrap_key.empty())
            return std::make_pair(nullptr, nullptr);

        return std::make_pair(config.default_wrap_key.c_str(),
                              SetEncryptKey(config, config.default_wrap_key));
    }

    AES_KEY *SetDecryptKey(const CertDatabaseConfig::AES256 &key) {
        if (AES_set_decrypt_key(key.data(), sizeof(key) * 8, &buffer) != 0)
            throw SslError("AES_set_decrypt_key() failed");

        return &buffer;
    }

    AES_KEY *SetDecryptKey(const CertDatabaseConfig &config,
                           const std::string &name) {
        const auto i = config.wrap_keys.find(name);
        if (i == config.wrap_keys.end())
            throw std::runtime_error("No such wrap_key: " + name);

        return SetDecryptKey(i->second);
    }
};

Pg::BinaryValue
UnwrapKey(Pg::BinaryValue key_der,
          const CertDatabaseConfig &config, const std::string &key_wrap_name,
          std::unique_ptr<unsigned char[]> &unwrapped);

#endif
