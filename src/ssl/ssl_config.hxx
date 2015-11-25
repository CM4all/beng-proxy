/*
 * SSL/TLS configuration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SSL_CONFIG_H
#define BENG_PROXY_SSL_CONFIG_H

#include <string>
#include <vector>

enum class SslVerify {
    NO,
    YES,
    OPTIONAL,
};

struct SslCertKeyConfig {
    std::string cert_file;

    std::string key_file;

    template<typename C, typename K>
    SslCertKeyConfig(C &&_cert_file, K &&_key_file)
        :cert_file(std::forward<C>(_cert_file)),
         key_file(std::forward<K>(_key_file)) {}
};

struct SslConfig {
    std::vector<SslCertKeyConfig> cert_key;

    std::string ca_cert_file;

    SslVerify verify = SslVerify::NO;

    bool IsValid() const {
        return !cert_key.empty();
    }
};

#endif
