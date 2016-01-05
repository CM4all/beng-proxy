/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SSL_CERT_NAME_CACHE_HXX
#define BENG_PROXY_SSL_CERT_NAME_CACHE_HXX

#include "pg/AsyncConnection.hxx"
#include "event/TimerEvent.hxx"

#include <unordered_set>
#include <string>
#include <mutex>

struct CertDatabaseConfig;

/**
 * A frontend for #CertDatabase which establishes a cache of all host
 * names and keeps it up to date.
 *
 * All modifications run asynchronously in the main thread, and
 * std::unordered_set queries may be executed from any thread
 * (protected by the mutex).
 */
class CertNameCache final : AsyncPgConnectionHandler {
    AsyncPgConnection conn;

    TimerEvent update_timer;

    mutable std::mutex mutex;

    /**
     * A list of host names found in the database.
     */
    std::unordered_set<std::string> names;

    /**
     * This flag is set to true as soon as the cached name list has
     * become complete for the first time.  With an incomplete cache,
     * Lookup() will always return true, because we don't know yet if
     * the desired name is just not yet loaded.
     */
    bool complete = false;

public:
    CertNameCache(const CertDatabaseConfig &config);

    ~CertNameCache() {
        update_timer.Cancel();
    }

    void Disconnect() {
        conn.Disconnect();
        update_timer.Cancel();
    }

    /**
     * Check if the given name exists in the database.
     */
    bool Lookup(const char *host) const;

private:
    void OnUpdateTimer();

    void ScheduleUpdate();

    void UnscheduleUpdate() {
        update_timer.Cancel();
    }

    /* virtual methods from AsyncPgConnectionHandler */
    void OnConnect() override;
    void OnDisconnect() override;
    void OnNotify(const char *name) override;
    void OnError(const char *prefix, const char *error) override;
};

#endif
