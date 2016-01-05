/*
 * Global declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_instance.hxx"
#include "lb_control.hxx"
#include "lb_listener.hxx"
#include "lb_config.hxx"
#include "ssl/Cache.hxx"

#include <assert.h>

lb_instance::lb_instance()
        :shutdown_listener(ShutdownCallback, this) {}

lb_instance::~lb_instance()
{
    assert(n_tcp_connections == 0);
}

CertCache &
lb_instance::GetCertCache(const LbCertDatabaseConfig &cert_db_config)
{
    auto i = cert_dbs.emplace(std::piecewise_construct,
                              std::forward_as_tuple(cert_db_config.name),
                              std::forward_as_tuple(cert_db_config));
    return i.first->second;
}

void
lb_instance::DisconnectCertCaches()
{
    for (auto &i : cert_dbs)
        i.second.Disconnect();
}
