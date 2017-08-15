/*
 * Set up load balancer objects.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Instance.hxx"
#include "Config.hxx"
#include "Listener.hxx"
#include "Control.hxx"
#include "ssl/Cache.hxx"

void
LbInstance::InitAllListeners()
{
    for (const auto &i : config.listeners) {
        listeners.emplace_front(*this, i);
        auto &listener = listeners.front();
        listener.Setup();
    }
}

void
LbInstance::DeinitAllListeners()
{
    listeners.clear();
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
LbInstance::InitAllControls()
{
    for (const auto &i : config.controls) {
        controls.emplace_front(*this);
        auto &control = controls.front();
        control.Open(i);
    }
}

void
LbInstance::DeinitAllControls()
{
    controls.clear();
}

void
LbInstance::EnableAllControls()
{
    for (auto &control : controls)
        control.Enable();
}
