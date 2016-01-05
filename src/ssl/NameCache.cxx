/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "NameCache.hxx"
#include "certdb/Config.hxx"
#include "event/Callback.hxx"

#include <daemon/log.h>

#include <assert.h>
#include <string.h>

CertNameCache::CertNameCache(const CertDatabaseConfig &config)
    :conn(config.connect.c_str(), config.schema.c_str(), *this),
     update_timer(MakeSimpleEventCallback(CertNameCache, OnUpdateTimer), this)
{
}

bool
CertNameCache::Lookup(const char *host) const
{
    if (!complete)
        /* we can't give reliable results until the cache is
           complete */
        return true;

    const std::unique_lock<std::mutex> lock(mutex);
    return names.find(host) != names.end();
}

void
CertNameCache::OnUpdateTimer()
{
    assert(conn.IsReady());

    daemon_log(4, "updating certificate database name cache\n");

    // TODO: implement
}

void
CertNameCache::ScheduleUpdate()
{
    static constexpr struct timeval update_delay = { 1, 0 };

    if (!update_timer.IsPending())
        update_timer.Add(update_delay);
}

void
CertNameCache::OnConnect()
{
    daemon_log(5, "connected to certificate database\n");

    conn.Execute("LISTEN modified");
    conn.Execute("LISTEN deleted");

    ScheduleUpdate();
}

void
CertNameCache::OnDisconnect()
{
    daemon_log(4, "disconnected from certificate database\n");

    UnscheduleUpdate();
}

void
CertNameCache::OnNotify(const char *name)
{
    daemon_log(5, "received notify '%s'\n", name);

    ScheduleUpdate();
}

void
CertNameCache::OnError(const char *prefix, const char *error)
{
    daemon_log(2, "%s: %s\n", prefix, error);
}
