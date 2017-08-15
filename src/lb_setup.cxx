/*
 * Set up load balancer objects.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_setup.hxx"
#include "lb/Config.hxx"
#include "lb_instance.hxx"
#include "lb/Listener.hxx"
#include "lb/Control.hxx"
#include "ssl/Cache.hxx"

void
init_all_listeners(LbInstance &instance)
{
    auto &listeners = instance.listeners;

    for (const auto &config : instance.config.listeners) {
        listeners.emplace_front(instance, config);
        auto &listener = listeners.front();
        listener.Setup();
    }
}

void
deinit_all_listeners(LbInstance *instance)
{
    instance->listeners.clear();
}

unsigned
LbInstance::FlushSSLSessionCache(long tm)
{
    unsigned n = 0;
    for (auto &listener : listeners)
        n += listener.FlushSSLSessionCache(tm);

    for (auto &db : cert_dbs)
        n += db.second.FlushSessionCache(tm);

    return n;
}

void
init_all_controls(LbInstance *instance)
{
    for (const auto &config : instance->config.controls) {
        instance->controls.emplace_front(*instance);
        auto &control = instance->controls.front();
        control.Open(config);
    }
}

void
deinit_all_controls(LbInstance *instance)
{
    instance->controls.clear();
}

void
enable_all_controls(LbInstance *instance)
{
    for (auto &control : instance->controls)
        control.Enable();
}
