// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "lb_check.hxx"
#include "lb/Config.hxx"
#include "ssl/Factory.hxx"
#include "ssl/CertCallback.hxx"

#include "lb_features.h"
#ifdef ENABLE_CERTDB
#include "ssl/Cache.hxx"
#endif

#ifdef HAVE_LUA
#include "lb/LuaHandler.hxx"
#include "lb/LuaInitHook.hxx"
#endif

static void
lb_check(EventLoop &event_loop, const LbCertDatabaseConfig &config)
{
#ifdef ENABLE_CERTDB
	CertCache cache(event_loop, config);

	for (const auto &ca_path : config.ca_certs)
		cache.LoadCaCertificate(ca_path.c_str());
#else
	(void)event_loop;
	(void)config;
#endif
}

static void
lb_check(const LbListenerConfig &config)
{
	if (config.ssl) {
		SslFactory(config.ssl_config, nullptr);
	}
}

void
lb_check(EventLoop &event_loop, const LbConfig &config)
{
	for (const auto &cdb : config.cert_dbs) {
		try {
			lb_check(event_loop, cdb.second);
		} catch (...) {
			std::throw_with_nested(std::runtime_error("cert_db '" + cdb.first + "'"));
		}
	}

	for (const auto &listener : config.listeners) {
		try {
			lb_check(listener);
		} catch (...) {
			std::throw_with_nested(std::runtime_error("listener '" + listener.name + "'"));
		}
	}

#ifdef HAVE_LUA
	{
		LbLuaInitHook init_hook(nullptr);

		for (const auto &i : config.lua_handlers)
			LbLuaHandler(event_loop, init_hook, i.second);
	}
#endif
}
