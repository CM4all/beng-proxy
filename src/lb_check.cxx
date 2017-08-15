/*
 * Implementation of --check.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_check.hxx"
#include "lb/Config.hxx"
#include "lb/LuaHandler.hxx"
#include "lb/LuaInitHook.hxx"
#include "ssl/ssl_factory.hxx"
#include "ssl/SniCallback.hxx"
#include "ssl/Cache.hxx"

static void
lb_check(EventLoop &event_loop, const LbCertDatabaseConfig &config)
{
    CertCache cache(event_loop, config);

    for (const auto &ca_path : config.ca_certs)
        cache.LoadCaCertificate(ca_path.c_str());
}

static void
lb_check(const LbListenerConfig &config)
{
    if (config.ssl) {
        auto *ssl = ssl_factory_new_server(config.ssl_config,
                                           std::unique_ptr<SslSniCallback>());
        ssl_factory_free(ssl);
    }
}

void
lb_check(EventLoop &event_loop, const LbConfig &config)
{
    for (const auto &cdb : config.cert_dbs) {
        try {
            lb_check(event_loop, cdb.second);
        } catch (...) {
            std::throw_with_nested(std::runtime_error("cert_db '" + cdb.first + "'"));
        }
    }

    for (const auto &listener : config.listeners) {
        try {
            lb_check(listener);
        } catch (...) {
            std::throw_with_nested(std::runtime_error("listener '" + listener.name + "'"));
        }
    }

    {
        LbLuaInitHook init_hook(nullptr);

        for (const auto &i : config.lua_handlers)
            LbLuaHandler(init_hook, i.second);
    }
}
