/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_LB_LISTENER_CONFIG_HXX
#define BENG_LB_LISTENER_CONFIG_HXX

#include "GotoConfig.hxx"
#include "ssl/ssl_config.hxx"
#include "net/AllocatedSocketAddress.hxx"

#include <string>

struct LbCertDatabaseConfig;

struct LbListenerConfig {
    std::string name;

    AllocatedSocketAddress bind_address;

    LbGotoConfig destination;

    /**
     * If non-empty, sets SO_BINDTODEVICE.
     */
    std::string interface;

    std::string tag;

    bool reuse_port = false;

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
