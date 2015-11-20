/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef CERT_DATABASE_CONFIG_HXX
#define CERT_DATABASE_CONFIG_HXX

#include <stdexcept>

struct CertDatabaseConfig {
    std::string connect;
    std::string schema;

    void Check() {
        if (connect.empty())
            throw std::runtime_error("Missing 'connect'");
    }
};

#endif
