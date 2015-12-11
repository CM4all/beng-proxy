/*
 * Implementation of --check.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_check.hxx"
#include "lb_config.hxx"
#include "ssl/ssl_factory.hxx"
#include "ssl/SniCallback.hxx"
#include "util/Error.hxx"

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
lb_check(const LbConfig &config)
{
    for (const auto &listener : config.listeners) {
        try {
            lb_check(listener);
        } catch (...) {
            std::throw_with_nested(std::runtime_error("listener '" + listener.name + "'"));
        }
    }
}
