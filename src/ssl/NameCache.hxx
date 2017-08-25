/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef BENG_PROXY_SSL_CERT_NAME_CACHE_HXX
#define BENG_PROXY_SSL_CERT_NAME_CACHE_HXX

#include "pg/AsyncConnection.hxx"
#include "event/TimerEvent.hxx"
#include "io/Logger.hxx"

#include <unordered_set>
#include <unordered_map>
#include <set>
#include <string>
#include <mutex>

struct CertDatabaseConfig;

class CertNameCacheHandler {
public:
    virtual void OnCertModified(const std::string &name, bool deleted) = 0;
};

/**
 * A frontend for #CertDatabase which establishes a cache of all host
 * names and keeps it up to date.
 *
 * All modifications run asynchronously in the main thread, and
 * std::unordered_set queries may be executed from any thread
 * (protected by the mutex).
 */
class CertNameCache final : Pg::AsyncConnectionHandler, Pg::AsyncResultHandler {
    const LLogger logger;

    CertNameCacheHandler &handler;

    Pg::AsyncConnection conn;

    TimerEvent update_timer;

    mutable std::mutex mutex;

    /**
     * A list of host names found in the database.
     */
    std::unordered_set<std::string> names;

    /**
     * A list of alt_names found in the database.  Each alt_name maps
     * to a list of common_name values it appears in.
     */
    std::unordered_map<std::string, std::set<std::string>> alt_names;

    /**
     * The latest timestamp seen in a record.  This is used for
     * incremental updates.
     */
    std::string latest = "1971-01-01";

    unsigned n_added, n_updated, n_deleted;

    /**
     * This flag is set to true as soon as the cached name list has
     * become complete for the first time.  With an incomplete cache,
     * Lookup() will always return true, because we don't know yet if
     * the desired name is just not yet loaded.
     */
    bool complete = false;

public:
    CertNameCache(EventLoop &event_loop,
                  const CertDatabaseConfig &config,
                  CertNameCacheHandler &_handler);

    ~CertNameCache() {
        update_timer.Cancel();
    }

    void Connect() {
        conn.Connect();
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

    void AddAltNames(const std::string &common_name,
                     std::list<std::string> &&list);
    void RemoveAltNames(const std::string &common_name,
                        std::list<std::string> &&list);

    /* virtual methods from Pg::AsyncConnectionHandler */
    void OnConnect() override;
    void OnDisconnect() override;
    void OnNotify(const char *name) override;
    void OnError(const char *prefix, const char *error) override;

    /* virtual methods from Pg::AsyncResultHandler */
    void OnResult(Pg::Result &&result) override;
    void OnResultEnd() override;
    void OnResultError() override;
};

#endif
