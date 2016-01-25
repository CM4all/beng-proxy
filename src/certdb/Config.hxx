/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef CERT_DATABASE_CONFIG_HXX
#define CERT_DATABASE_CONFIG_HXX

#include <map>
#include <string>
#include <array>
#include <stdexcept>

struct CertDatabaseConfig {
    std::string connect;
    std::string schema;

    typedef std::array<unsigned char, 256/8> AES256;

    std::map<std::string, AES256> wrap_keys;

    std::string default_wrap_key;

    void Check() {
        if (connect.empty())
            throw std::runtime_error("Missing 'connect'");
    }
};

#endif
