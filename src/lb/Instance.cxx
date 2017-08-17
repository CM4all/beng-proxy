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

#include "Instance.hxx"
#include "lb/Control.hxx"
#include "lb/Config.hxx"
#include "lb/Listener.hxx"
#include "ssl/Cache.hxx"
#include "fb_pool.hxx"
#include "event/Duration.hxx"
#include "access_log/Glue.hxx"

#include <assert.h>
#include <sys/signal.h>

static constexpr auto &COMPRESS_INTERVAL = EventDuration<600>::value;

LbInstance::LbInstance(const LbConfig &_config)
    :config(_config),
     avahi_client(event_loop, "beng-lb"),
     goto_map(config, avahi_client),
     monitors(root_pool),
     compress_event(event_loop, BIND_THIS_METHOD(OnCompressTimer)),
     shutdown_listener(event_loop, BIND_THIS_METHOD(ShutdownCallback)),
     sighup_event(event_loop, SIGHUP, BIND_THIS_METHOD(ReloadEventCallback))
{
}

LbInstance::~LbInstance()
{
    assert(tcp_connections.empty());
    assert(http_connections.empty());
}

void
LbInstance::InitWorker()
{
    compress_event.Add(COMPRESS_INTERVAL);

    for (auto &listener : listeners)
        listener.Scan(goto_map);

    CreateMonitors();

    /* run monitors only in the worker process */
    monitors.Enable();

    ConnectCertCaches();
}

void
LbInstance::CreateMonitors()
{
    goto_map.ForEachCluster([this](const LbCluster &_cluster){
            const auto &cluster = _cluster.GetConfig();
            if (cluster.monitor == nullptr)
                return;

            for (const auto &member : cluster.members)
                monitors.Add(*member.node, member.port, *cluster.monitor,
                             event_loop);
        });
}

void
LbInstance::Compress()
{
    fb_pool_compress();

    for (auto &i : cert_dbs)
        i.second.Expire();

    unsigned n_ssl_sessions = FlushSSLSessionCache(time(nullptr));
    logger(3, "flushed ", n_ssl_sessions, " SSL sessions");
}

CertCache &
LbInstance::GetCertCache(const LbCertDatabaseConfig &cert_db_config)
{
    auto i = cert_dbs.emplace(std::piecewise_construct,
                              std::forward_as_tuple(cert_db_config.name),
                              std::forward_as_tuple(event_loop,
                                                    cert_db_config));
    if (i.second)
        for (const auto &j : cert_db_config.ca_certs)
            i.first->second.LoadCaCertificate(j.c_str());

    return i.first->second;
}

void
LbInstance::ConnectCertCaches()
{
    for (auto &i : cert_dbs)
        i.second.Connect();
}

void
LbInstance::DisconnectCertCaches()
{
    for (auto &i : cert_dbs)
        i.second.Disconnect();
}

void
LbInstance::OnCompressTimer()
{
    Compress();

    compress_event.Add(COMPRESS_INTERVAL);
}
