// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "lb_check.hxx"
#include "lb/Config.hxx"
#include "ssl/Factory.hxx"
#include "ssl/CertCallback.hxx"
#include "lib/fmt/RuntimeError.hxx"

#include "lb_features.h"
#ifdef ENABLE_CERTDB
#include "ssl/Cache.hxx"
#endif

#ifdef HAVE_LUA
#include "lb/LuaHandler.hxx"
#include "lb/LuaInitHook.hxx"
#endif

using std::string_view_literals::operator""sv;

#ifdef ENABLE_CERTDB

static void
lb_check(EventLoop &event_loop, const LbCertDatabaseConfig &config)
{
	CertCache cache(event_loop, config);

	for (const auto &ca_path : config.ca_certs)
		cache.LoadCaCertificate(ca_path.c_str());
}

#endif

static void
lb_check(const LbListenerConfig &config)
{
	if (config.ssl) {
		SslFactory(config.ssl_config, nullptr);
	}
}

void
lb_check([[maybe_unused]] EventLoop &event_loop, const LbConfig &config)
{
#ifdef ENABLE_CERTDB
	for (const auto &cdb : config.cert_dbs) {
		try {
			lb_check(event_loop, cdb.second);
		} catch (...) {
			std::throw_with_nested(FmtRuntimeError("cert_db {:?}"sv, cdb.first));
		}
	}
#endif

	for (const auto &listener : config.listeners) {
		try {
			lb_check(listener);
		} catch (...) {
			std::throw_with_nested(FmtRuntimeError("listener {:?}"sv, listener.name));
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
