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

    // TODO: make asynchronous

    constexpr unsigned limit = 1000;

    const auto result =
        conn.ExecuteParams("SELECT common_name, deleted, modified "
                           " FROM server_certificates"
                           " WHERE modified>$1"
                           " ORDER BY modified"
                           " LIMIT 1000",
                           latest.c_str());
    if (result.IsError()) {
        daemon_log(1, "query error from certificate database: %s\n",
                   result.GetErrorMessage());
        return;
    }

    unsigned n_added = 0, n_updated = 0, n_deleted = 0;

    const char *modified = nullptr;

    for (const auto &row : result) {
        std::string name(row.GetValue(0));
        const bool deleted = *row.GetValue(1) == 't';
        modified = row.GetValue(2);

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

    daemon_log(4, "certificate database name cache: %u added, %u updated, %u deleted\n",
               n_added, n_updated, n_deleted);

    if (modified != nullptr)
        latest = modified;

    if (result.GetRowCount() == limit)
        /* run again until no more updated records are received */
        ScheduleUpdate();
    else if (!complete) {
        daemon_log(4, "certificate database name cache is complete\n");
        complete = true;
    }

    conn.CheckNotify();
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
