/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_LB_LISTENER_CONFIG_HXX
#define BENG_LB_LISTENER_CONFIG_HXX

#include "GotoConfig.hxx"
#include "ssl/ssl_config.hxx"
#include "net/ListenerConfig.hxx"

#include <string>

struct LbCertDatabaseConfig;

struct LbListenerConfig : ListenerConfig {
    std::string name;

    LbGotoConfig destination;

    std::string tag;

    bool verbose_response = false;

    bool ssl = false;

    SslConfig ssl_config;

    const LbCertDatabaseConfig *cert_db = nullptr;

    explicit LbListenerConfig(const char *_name)
        :name(_name) {}

    gcc_pure
    bool HasZeroConf() const {
        return destination.HasZeroConf();
    }
};

#endif
