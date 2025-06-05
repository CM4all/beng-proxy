// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Instance.hxx"
#include "HttpConnection.hxx"
#include "TcpConnection.hxx"
#include "Control.hxx"
#include "Config.hxx"
#include "CommandLine.hxx"
#include "Listener.hxx"
#include "fs/Stock.hxx"
#include "fs/Balancer.hxx"
#include "ssl/Client.hxx"
#include "cluster/BalancerMap.hxx"
#include "memory/fb_pool.hxx"
#include "pipe/Stock.hxx"
#include "access_log/Glue.hxx"
#include "util/PrintException.hxx"

#include "lb_features.h"
#ifdef ENABLE_CERTDB
#include "ssl/Cache.hxx"
#endif

#ifdef HAVE_AVAHI
#include "lib/avahi/Client.hxx"
#include "lib/avahi/Publisher.hxx"
#endif

#include <assert.h>
#include <sys/signal.h>

static constexpr Event::Duration COMPRESS_INTERVAL = std::chrono::minutes(10);

LbInstance::LbInstance(const LbConfig &_config)
	:config(_config),
	 shutdown_listener(event_loop, BIND_THIS_METHOD(ShutdownCallback)),
	 sighup_event(event_loop, SIGHUP, BIND_THIS_METHOD(ReloadEventCallback)),
	 compress_event(event_loop, BIND_THIS_METHOD(OnCompressTimer)),
	 balancer(new BalancerMap()),
	 fs_stock(new FilteredSocketStock(event_loop,
					  config.tcp_stock_limit,
					  config.tcp_stock_max_idle)),
	 fs_balancer(new FilteredSocketBalancer(*fs_stock, failure_manager)),
	 ssl_client_factory(new SslClientFactory(config.ssl_client)),
	 pipe_stock(new PipeStock(event_loop)),
	 monitors(event_loop, failure_manager),
	 goto_map(config,
		  {failure_manager,
		   *balancer, *fs_stock, *fs_balancer,
		   *ssl_client_factory,
		   monitors,
#ifdef HAVE_AVAHI
		   avahi_client, *this,
#endif
		  },
		  event_loop)
{
}

LbInstance::~LbInstance() noexcept
{
	assert(tcp_connections.empty());
	assert(http_connections.empty());
}

void
LbInstance::InitWorker()
{
	compress_event.Schedule(COMPRESS_INTERVAL);

	for (auto &listener : listeners)
		listener.Scan(goto_map);

	goto_map.SetInstance(*this);

#ifdef ENABLE_CERTDB
	ConnectCertCaches();
#endif
}

#ifdef HAVE_AVAHI

Avahi::Client &
LbInstance::GetAvahiClient()
{
	if (!avahi_client) {
		Avahi::ErrorHandler &error_handler = *this;
		avahi_client = std::make_unique<Avahi::Client>(event_loop,
							       error_handler);
	}

	return *avahi_client;
}

Avahi::Publisher &
LbInstance::GetAvahiPublisher()
{
	if (!avahi_publisher) {
		Avahi::ErrorHandler &error_handler = *this;
		avahi_publisher = std::make_unique<Avahi::Publisher>(GetAvahiClient(),
								     "beng-lb",
								     error_handler);
	}

	return *avahi_publisher;
}

#endif // HAVE_AVAHI

void
LbInstance::ReloadState() noexcept
{
#ifdef HAVE_AVAHI
	for (auto &i : listeners) {
		const auto &c = i.GetConfig();
		if (c.name.empty())
			continue;

		if (c.HasZeroconfPublisher()) {
			const auto path = fmt::format("beng-lb/listener/{}/zeroconf", c.name);
			i.SetZeroconfVisible(state_directories.GetBool(path.c_str(), true));
		}
	}
#endif // HAVE_AVAHI
}

void
LbInstance::Compress() noexcept
{
	fb_pool_compress();
	request_slice_pool.Compress();

#ifdef ENABLE_CERTDB
	for (auto &i : cert_dbs)
		i.second.Expire();
#endif
}

#ifdef ENABLE_CERTDB

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
LbInstance::DisconnectCertCaches() noexcept
{
	for (auto &i : cert_dbs)
		i.second.Disconnect();
}

#endif

void
LbInstance::OnCompressTimer() noexcept
{
	Compress();

	compress_event.Schedule(COMPRESS_INTERVAL);
}

bool
LbInstance::OnAvahiError(std::exception_ptr e) noexcept
{
	PrintException(e);
	return true;
}
