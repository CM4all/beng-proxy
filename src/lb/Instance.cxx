/*
 * Copyright 2007-2022 CM4all GmbH
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
#include "HttpConnection.hxx"
#include "TcpConnection.hxx"
#include "Control.hxx"
#include "Config.hxx"
#include "CommandLine.hxx"
#include "Listener.hxx"
#include "fs/Stock.hxx"
#include "fs/Balancer.hxx"
#include "cluster/BalancerMap.hxx"
#include "memory/fb_pool.hxx"
#include "pipe_stock.hxx"
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
					  config.tcp_stock_limit)),
	 fs_balancer(new FilteredSocketBalancer(*fs_stock, failure_manager)),
	 pipe_stock(new PipeStock(event_loop)),
	 monitors(event_loop, failure_manager),
	 goto_map(config,
		  {failure_manager,
		   *balancer, *fs_stock, *fs_balancer,
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

void
LbInstance::Compress() noexcept
{
	fb_pool_compress();

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
