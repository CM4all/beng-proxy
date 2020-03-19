/*
 * Copyright 2007-2020 CM4all GmbH
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

#include "lb_check.hxx"
#include "lb/Config.hxx"
#include "lb/LuaHandler.hxx"
#include "lb/LuaInitHook.hxx"
#include "ssl/Factory.hxx"
#include "ssl/SniCallback.hxx"
#include "ssl/Cache.hxx"

static void
lb_check(EventLoop &event_loop, const LbCertDatabaseConfig &config)
{
	CertCache cache(event_loop, config);

	for (const auto &ca_path : config.ca_certs)
		cache.LoadCaCertificate(ca_path.c_str());
}

static void
lb_check(const LbListenerConfig &config)
{
	if (config.ssl) {
		auto *ssl = ssl_factory_new_server(config.ssl_config,
						   std::unique_ptr<SslSniCallback>());
		ssl_factory_free(ssl);
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

	{
		LbLuaInitHook init_hook(nullptr);

		for (const auto &i : config.lua_handlers)
			LbLuaHandler(init_hook, i.second);
	}
}
