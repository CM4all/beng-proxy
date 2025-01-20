// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "LConfig.hxx"
#include "access_log/Config.hxx"
#include "ssl/Config.hxx"
#include "http/CookieSameSite.hxx"
#include "net/LocalSocketAddress.hxx"
#include "net/SocketConfig.hxx"
#include "spawn/Config.hxx"
#include "config.h"

#include <chrono>
#include <forward_list>
#include <map>
#include <string_view>

#include <stddef.h>

/**
 * Configuration.
 */
struct BpConfig {
	std::forward_list<BpListenerConfig> listen;

	MultiAccessLogConfig access_log;

	AccessLogConfig child_error_log;

	std::string session_cookie = "beng_proxy_session";

	std::chrono::seconds session_idle_timeout = std::chrono::minutes(30);

	std::string session_save_path;

	struct ControlListener : SocketConfig {
		ControlListener()
			:SocketConfig{
				.pass_cred = true,
			}
		{
		}

		explicit ControlListener(SocketAddress _bind_address)
			:SocketConfig{
				.bind_address = AllocatedSocketAddress{_bind_address},
				.pass_cred = true,
			}
		{
		}
	};

	std::forward_list<ControlListener> control_listen;

	std::forward_list<LocalSocketAddress> translation_sockets;

	/** maximum number of simultaneous connections */
	unsigned max_connections = 32768;

	size_t http_cache_size = 512 * 1024 * 1024;

	size_t filter_cache_size = 128 * 1024 * 1024;

	std::size_t encoding_cache_size = 0;

	unsigned translate_cache_size = 131072;
	unsigned translate_stock_limit = 32;

	unsigned tcp_stock_limit = 0;
	static constexpr std::size_t tcp_stock_max_idle = 16;

	unsigned lhttp_stock_limit = 0, lhttp_stock_max_idle = 8;
	unsigned fcgi_stock_limit = 0, fcgi_stock_max_idle = 8;

	unsigned was_stock_limit = 0, was_stock_max_idle = 16;
	unsigned multi_was_stock_limit = 0, multi_was_stock_max_idle = 16;
	unsigned remote_was_stock_limit = 0, remote_was_stock_max_idle = 16;

	unsigned cluster_size = 0, cluster_node = 0;

	int io_uring_sq_thread_cpu = -1;

	CookieSameSite session_cookie_same_site = CookieSameSite::DEFAULT;

	bool dynamic_session_cookie = false;

	bool verbose_response = false;

	bool emulate_mod_auth_easy = false;

	bool http_cache_obey_no_cache = true;

	bool use_xattr = false;

	bool use_io_uring = true;

	bool io_uring_sqpoll = false;

	SpawnConfig spawn;

	SslClientConfig ssl_client;

	BpConfig() {
#ifdef HAVE_LIBSYSTEMD
		spawn.systemd_scope = "bp-spawn.scope";
		spawn.systemd_scope_description = "The cm4all-beng-proxy child process spawner";
		spawn.systemd_slice = "system-cm4all.slice";
#endif
	}

	void HandleSet(std::string_view name, const char *value);

	void Finish(unsigned default_port);
};

/**
 * Load and parse the specified configuration file.  Throws an
 * exception on error.
 */
void
LoadConfigFile(BpConfig &config, const char *path);
