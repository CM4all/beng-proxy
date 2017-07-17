/*
 * Global declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_instance.hxx"
#include "lb/Control.hxx"
#include "lb_config.hxx"
#include "lb/Listener.hxx"
#include "ssl/Cache.hxx"
#include "fb_pool.hxx"
#include "event/Duration.hxx"

#include <daemon/log.h>

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
    daemon_log(3, "flushed %u SSL sessions\n", n_ssl_sessions);
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
