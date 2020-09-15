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

#pragma once

#include "access_log/Config.hxx"
#include "ssl/Config.hxx"
#include "net/SocketConfig.hxx"
#include "spawn/Config.hxx"

#include <forward_list>
#include <chrono>

#include <stddef.h>

struct StringView;

/**
 * Configuration.
 */
struct BpConfig {
	struct Listener : SocketConfig {
		std::string tag;

#ifdef HAVE_AVAHI
		std::string zeroconf_service;
#endif

		bool auth_alt_host = false;

		bool ssl = false;

		SslConfig ssl_config;

		Listener() {
			listen = 64;
			tcp_defer_accept = 10;
		}

		explicit Listener(SocketAddress _address) noexcept
			:SocketConfig(_address)
		{
			listen = 64;
			tcp_defer_accept = 10;
		}
	};

	std::forward_list<Listener> listen;

	AccessLogConfig access_log;

	AccessLogConfig child_error_log;

	std::string session_cookie = "beng_proxy_session";

	std::chrono::seconds session_idle_timeout = std::chrono::minutes(30);

	std::string session_save_path;

	struct ControlListener : SocketConfig {
		ControlListener() {
			pass_cred = true;
		}

		explicit ControlListener(SocketAddress _bind_address)
			:SocketConfig(_bind_address) {
			pass_cred = true;
		}
	};

	std::forward_list<ControlListener> control_listen;

	const char *document_root = "/var/www";

	std::forward_list<AllocatedSocketAddress> translation_sockets;

	/** maximum number of simultaneous connections */
	unsigned max_connections = 32768;

	size_t http_cache_size = 512 * 1024 * 1024;

	size_t filter_cache_size = 128 * 1024 * 1024;

	size_t nfs_cache_size = 256 * 1024 * 1024;

	unsigned translate_cache_size = 131072;
	unsigned translate_stock_limit = 64;

	unsigned tcp_stock_limit = 0;

	unsigned fcgi_stock_limit = 0, fcgi_stock_max_idle = 8;

	unsigned was_stock_limit = 0, was_stock_max_idle = 16;

	unsigned cluster_size = 0, cluster_node = 0;

	enum class SessionCookieSameSite : uint8_t {
		NONE,
		STRICT,
		LAX,
	} session_cookie_same_site;

	bool dynamic_session_cookie = false;

	bool verbose_response = false;

	bool emulate_mod_auth_easy = false;

	bool http_cache_obey_no_cache = true;

	SpawnConfig spawn;

	SslClientConfig ssl_client;

	BpConfig() {
#ifdef HAVE_LIBSYSTEMD
		spawn.systemd_scope = "bp-spawn.scope";
		spawn.systemd_scope_description = "The cm4all-beng-proxy child process spawner";
		spawn.systemd_slice = "system-cm4all.slice";
#endif
	}

	void HandleSet(StringView name, const char *value);

	void Finish(const UidGid &user, unsigned default_port);
};

/**
 * Load and parse the specified configuration file.  Throws an
 * exception on error.
 */
void
LoadConfigFile(BpConfig &config, const char *path);
