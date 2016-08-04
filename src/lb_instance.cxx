/*
 * Global declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_instance.hxx"
#include "lb_control.hxx"
#include "lb_listener.hxx"
#include "lb_hmonitor.hxx"
#include "lb_config.hxx"
#include "ssl/Cache.hxx"
#include "fb_pool.hxx"
#include "event/Duration.hxx"

#include <assert.h>
#include <sys/signal.h>

static constexpr auto &COMPRESS_INTERVAL = EventDuration<600>::value;

LbInstance::LbInstance()
    :monitors(pool),
     avahi_client(event_loop, "beng-lb"),
     child_process_registry(event_loop),
     launch_worker_event(event_loop, BIND_THIS_METHOD(LaunchWorker)),
     compress_event(event_loop, BIND_THIS_METHOD(OnCompressTimer)),
     shutdown_listener(event_loop, BIND_THIS_METHOD(ShutdownCallback)),
     sighup_event(event_loop, SIGHUP, BIND_THIS_METHOD(ReloadEventCallback))
{
}

LbInstance::~LbInstance()
{
    assert(n_tcp_connections == 0);
}

void
LbInstance::InitWorker()
{
    compress_event.Add(COMPRESS_INTERVAL);

    /* run monitors only in the worker process */
    monitors.Enable();

    clusters.Scan(*config, avahi_client);

    ConnectCertCaches();
}

void
LbInstance::Compress()
{
    fb_pool_compress();
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
