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
#include "event/Callback.hxx"

#include <assert.h>

static constexpr auto &COMPRESS_INTERVAL = EventDuration<600>::value;

lb_instance::lb_instance()
    :compress_event(MakeSimpleEventCallback(lb_instance, OnCompressTimer),
                    this),
     shutdown_listener(ShutdownCallback, this)
{
}

lb_instance::~lb_instance()
{
    assert(n_tcp_connections == 0);
}

void
lb_instance::InitWorker()
{
    compress_event.Add(COMPRESS_INTERVAL);

    /* run monitors only in the worker process */
    lb_hmonitor_enable();

    ConnectCertCaches();
}

void
lb_instance::Compress()
{
    CycleBuffers();
    fb_pool_compress();
}

CertCache &
lb_instance::GetCertCache(const LbCertDatabaseConfig &cert_db_config)
{
    auto i = cert_dbs.emplace(std::piecewise_construct,
                              std::forward_as_tuple(cert_db_config.name),
                              std::forward_as_tuple(cert_db_config));
    if (i.second)
        for (const auto &j : cert_db_config.ca_certs)
            i.first->second.LoadCaCertificate(j.c_str());

    return i.first->second;
}

void
lb_instance::ConnectCertCaches()
{
    for (auto &i : cert_dbs)
        i.second.Connect();
}

void
lb_instance::DisconnectCertCaches()
{
    for (auto &i : cert_dbs)
        i.second.Disconnect();
}

void
lb_instance::CycleBuffers()
{
    for (auto &connection : connections)
        connection.CycleBuffers();
}

void
lb_instance::OnCompressTimer()
{
    Compress();

    compress_event.Add(COMPRESS_INTERVAL);
}
