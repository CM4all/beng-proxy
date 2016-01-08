/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "NameCache.hxx"
#include "certdb/Config.hxx"
#include "event/Callback.hxx"

#include <daemon/log.h>

#include <assert.h>
#include <string.h>

CertNameCache::CertNameCache(const CertDatabaseConfig &config,
                             CertNameCacheHandler &_handler)
    :handler(_handler),
     conn(config.connect.c_str(), config.schema.c_str(), *this),
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

    n_added = n_updated = n_deleted = 0;

    if (complete)
        conn.SendQuery(*this,
                       "SELECT common_name, modified, deleted "
                       " FROM server_certificates"
                       " WHERE modified>$1"
                       " ORDER BY modified",
                       latest.c_str());
    else
        /* omit deleted certificates during the initial download
           (until our mirror is complete) */
        conn.SendQuery(*this,
                       "SELECT common_name, modified "
                       " FROM server_certificates"
                       " WHERE NOT deleted"
                       " ORDER BY modified");

    conn.SetSingleRowMode();
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

    // TODO: make asynchronous
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

void
CertNameCache::OnResult(PgResult &&result)
{
    if (result.IsError()) {
        daemon_log(1, "query error from certificate database: %s\n",
                   result.GetErrorMessage());
        ScheduleUpdate();
        return;
    }

    const char *modified = nullptr;

    for (const auto &row : result) {
        std::string name(row.GetValue(0));
        modified = row.GetValue(1);
        const bool deleted = complete && *row.GetValue(2) == 't';

        handler.OnCertModified(name, deleted);

        if (deleted) {
            auto i = names.find(std::move(name));
            if (i != names.end()) {
                names.erase(i);
                ++n_deleted;
            }
        } else {
            auto i = names.emplace(std::move(name));
            if (i.second)
                ++n_added;
            else
                ++n_updated;
        }
    }

    if (modified != nullptr)
        latest = modified;
}

void
CertNameCache::OnResultEnd()
{
    daemon_log(4, "certificate database name cache: %u added, %u updated, %u deleted\n",
               n_added, n_updated, n_deleted);

    if (!complete) {
        daemon_log(4, "certificate database name cache is complete\n");
        complete = true;
    }
}

void
CertNameCache::OnResultError()
{
    ScheduleUpdate();
}
