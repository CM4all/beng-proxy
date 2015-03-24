/*
 * Implementation of --check.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_check.hxx"
#include "lb_config.hxx"
#include "ssl_factory.hxx"
#include "util/Error.hxx"

static bool
lb_check(const struct lb_listener_config &config, Error &error)
{
    if (config.ssl) {
        auto *ssl = ssl_factory_new(config.ssl_config, true, error);
        if (ssl == nullptr)
            return false;

        ssl_factory_free(ssl);
    }

    return true;
}

bool
lb_check(const struct lb_config &config, Error &error)
{
    for (const auto &listener : config.listeners) {
        if (!lb_check(listener, error)) {
            error.FormatPrefix("listener '%s': ", listener.name.c_str());
            return false;
        }
    }

    return true;
}
