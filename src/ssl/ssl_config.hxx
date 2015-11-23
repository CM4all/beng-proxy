/*
 * SSL/TLS configuration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SSL_CONFIG_H
#define BENG_PROXY_SSL_CONFIG_H

#include <string>
#include <vector>

enum class ssl_verify {
    NO,
    YES,
    OPTIONAL,
};

struct ssl_cert_key_config {
    std::string cert_file;

    std::string key_file;

    template<typename C, typename K>
    ssl_cert_key_config(C &&_cert_file, K &&_key_file)
        :cert_file(std::forward<C>(_cert_file)),
         key_file(std::forward<K>(_key_file)) {}
};

struct ssl_config {
    std::vector<ssl_cert_key_config> cert_key;

    std::string ca_cert_file;

    ssl_verify verify;

    ssl_config():verify(ssl_verify::NO) {}

    bool IsValid() const {
        return !cert_key.empty();
    }
};

#endif
