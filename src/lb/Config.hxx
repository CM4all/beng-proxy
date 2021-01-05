/*
 * Copyright 2007-2021 CM4all GmbH
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

#include "lb/ListenerConfig.hxx"
#include "lb/GotoConfig.hxx"
#include "lb/ClusterConfig.hxx"
#include "lb/MonitorConfig.hxx"
#include "access_log/Config.hxx"
#include "net/SocketConfig.hxx"
#include "certdb/Config.hxx"

#include <map>
#include <list>
#include <string>
#include <memory>

struct LbHttpCheckConfig;

struct LbControlConfig : SocketConfig {
	LbControlConfig() noexcept {
		pass_cred = true;
	}
};

struct LbCertDatabaseConfig : CertDatabaseConfig {
	std::string name;

	/**
	 * List of PEM path names containing certificator authorities
	 * we're going to use to build the certificate chain.
	 */
	std::list<std::string> ca_certs;

	explicit LbCertDatabaseConfig(const char *_name) noexcept
		:name(_name) {}
};

struct LbConfig {
	AccessLogConfig access_log;

	std::list<LbControlConfig> controls;

	std::map<std::string, LbCertDatabaseConfig> cert_dbs;

	std::map<std::string, LbMonitorConfig> monitors;

	std::map<std::string, LbNodeConfig> nodes;

	std::map<std::string, LbClusterConfig> clusters;
	std::map<std::string, LbBranchConfig> branches;
	std::map<std::string, LbLuaHandlerConfig> lua_handlers;
	std::map<std::string, LbTranslationHandlerConfig> translation_handlers;

	std::list<LbListenerConfig> listeners;

	std::unique_ptr<LbHttpCheckConfig> global_http_check;

	LbConfig() noexcept;
	~LbConfig() noexcept;

	template<typename T>
	gcc_pure
	const LbMonitorConfig *FindMonitor(T &&t) const noexcept {
		const auto i = monitors.find(std::forward<T>(t));
		return i != monitors.end()
			? &i->second
			: nullptr;
	}

	template<typename T>
	gcc_pure
	const LbCertDatabaseConfig *FindCertDb(T &&t) const noexcept {
		const auto i = cert_dbs.find(std::forward<T>(t));
		return i != cert_dbs.end()
			? &i->second
			: nullptr;
	}

	template<typename T>
	gcc_pure
	const LbNodeConfig *FindNode(T &&t) const noexcept {
		const auto i = nodes.find(std::forward<T>(t));
		return i != nodes.end()
			? &i->second
			: nullptr;
	}

	template<typename T>
	gcc_pure
	const LbClusterConfig *FindCluster(T &&t) const noexcept {
		const auto i = clusters.find(std::forward<T>(t));
		return i != clusters.end()
			? &i->second
			: nullptr;
	}

	template<typename T>
	gcc_pure
	LbGotoConfig FindGoto(T &&t) const noexcept {
		const auto *cluster = FindCluster(t);
		if (cluster != nullptr)
			return LbGotoConfig(*cluster);

		const auto *branch = FindBranch(t);
		if (branch != nullptr)
			return LbGotoConfig(*branch);

		const auto *lua = FindLuaHandler(t);
		if (lua != nullptr)
			return LbGotoConfig(*lua);

		const auto *translation = FindTranslationHandler(t);
		if (translation != nullptr)
			return LbGotoConfig(*translation);

		return {};
	}

	template<typename T>
	gcc_pure
	const LbBranchConfig *FindBranch(T &&t) const noexcept {
		const auto i = branches.find(std::forward<T>(t));
		return i != branches.end()
			? &i->second
			: nullptr;
	}

	template<typename T>
	gcc_pure
	const LbLuaHandlerConfig *FindLuaHandler(T &&t) const noexcept {
		const auto i = lua_handlers.find(std::forward<T>(t));
		return i != lua_handlers.end()
			? &i->second
			: nullptr;
	}

	template<typename T>
	gcc_pure
	const LbTranslationHandlerConfig *FindTranslationHandler(T &&t) const noexcept {
		const auto i = translation_handlers.find(std::forward<T>(t));
		return i != translation_handlers.end()
			? &i->second
			: nullptr;
	}

	template<typename T>
	gcc_pure
	const LbListenerConfig *FindListener(T &&t) const noexcept {
		for (const auto &i : listeners)
			if (i.name == t)
				return &i;

		return nullptr;
	}

	bool HasCertDatabase() const noexcept {
		for (const auto &i : listeners)
			if (i.cert_db != nullptr)
				return true;

		return false;
	}

	gcc_pure
	bool HasZeroConf() const noexcept {
#ifdef HAVE_AVAHI
		for (const auto &i : listeners)
			if (i.HasZeroConf())
				return true;
#endif

		return false;
	}

	gcc_pure
	bool HasTransparentSource() const noexcept {
		for (const auto &i : clusters)
			if (i.second.transparent_source)
				return true;

		return false;
	}
};

/**
 * Load and parse the specified configuration file.  Throws an
 * exception on error.
 */
void
LoadConfigFile(LbConfig &config, const char *path);
