/*
 * author: Max Kellermann <mk@cm4all.com>
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

PgBinaryValue
UnwrapKey(PgBinaryValue key_der,
          const CertDatabaseConfig &config, const std::string &key_wrap_name,
          std::unique_ptr<unsigned char[]> &unwrapped);

#endif
