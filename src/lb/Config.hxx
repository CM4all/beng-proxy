// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "ListenerConfig.hxx"
#include "GotoConfig.hxx"
#include "ClusterConfig.hxx"
#include "MonitorConfig.hxx"
#include "PrometheusExporterConfig.hxx"
#include "access_log/Config.hxx"
#include "ssl/Config.hxx"
#include "net/SocketConfig.hxx"
#include "certdb/Config.hxx"

#ifdef HAVE_AVAHI
#include "PrometheusDiscoveryConfig.hxx"
#endif

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
	 * List of PEM path names containing certificate authorities
	 * we're going to use to build the certificate chain.
	 */
	std::list<std::string> ca_certs;

	explicit LbCertDatabaseConfig(const char *_name) noexcept
		:name(_name) {}
};

struct LbConfig {
	AccessLogConfig access_log;

	std::list<LbControlConfig> controls;

	// TODO there is no ConfigParser for this yet
	SslClientConfig ssl_client;

	std::map<std::string, LbCertDatabaseConfig> cert_dbs;

	std::map<std::string, LbMonitorConfig> monitors;

	std::map<std::string, LbNodeConfig> nodes;

	std::map<std::string, LbClusterConfig> clusters;
	std::map<std::string, LbBranchConfig> branches;
	std::map<std::string, LbLuaHandlerConfig> lua_handlers;
	std::map<std::string, LbTranslationHandlerConfig> translation_handlers;
	std::map<std::string, LbPrometheusExporterConfig> prometheus_exporters;

#ifdef HAVE_AVAHI
	std::map<std::string, LbPrometheusDiscoveryConfig> prometheus_discoveries;
#endif

	std::list<LbListenerConfig> listeners;

	std::unique_ptr<LbHttpCheckConfig> global_http_check;

	unsigned tcp_stock_limit = 256;

	LbConfig() noexcept;
	~LbConfig() noexcept;

	template<typename T>
	[[gnu::pure]]
	const LbMonitorConfig *FindMonitor(T &&t) const noexcept {
		const auto i = monitors.find(std::forward<T>(t));
		return i != monitors.end()
			? &i->second
			: nullptr;
	}

	template<typename T>
	[[gnu::pure]]
	const LbCertDatabaseConfig *FindCertDb(T &&t) const noexcept {
		const auto i = cert_dbs.find(std::forward<T>(t));
		return i != cert_dbs.end()
			? &i->second
			: nullptr;
	}

	template<typename T>
	[[gnu::pure]]
	const LbNodeConfig *FindNode(T &&t) const noexcept {
		const auto i = nodes.find(std::forward<T>(t));
		return i != nodes.end()
			? &i->second
			: nullptr;
	}

	template<typename T>
	[[gnu::pure]]
	const LbClusterConfig *FindCluster(T &&t) const noexcept {
		const auto i = clusters.find(std::forward<T>(t));
		return i != clusters.end()
			? &i->second
			: nullptr;
	}

	template<typename T>
	[[gnu::pure]]
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

		if (const auto *e = FindPrometheusExporter(t))
			return LbGotoConfig(*e);

#ifdef HAVE_AVAHI
		if (const auto *d = FindPrometheusDiscovery(t))
			return LbGotoConfig(*d);
#endif // HAVE_AVAHI

		return {};
	}

	template<typename T>
	[[gnu::pure]]
	const LbBranchConfig *FindBranch(T &&t) const noexcept {
		const auto i = branches.find(std::forward<T>(t));
		return i != branches.end()
			? &i->second
			: nullptr;
	}

	template<typename T>
	[[gnu::pure]]
	const LbLuaHandlerConfig *FindLuaHandler(T &&t) const noexcept {
		const auto i = lua_handlers.find(std::forward<T>(t));
		return i != lua_handlers.end()
			? &i->second
			: nullptr;
	}

	template<typename T>
	[[gnu::pure]]
	const LbTranslationHandlerConfig *FindTranslationHandler(T &&t) const noexcept {
		const auto i = translation_handlers.find(std::forward<T>(t));
		return i != translation_handlers.end()
			? &i->second
			: nullptr;
	}

	template<typename T>
	[[gnu::pure]]
	const LbPrometheusExporterConfig *FindPrometheusExporter(T &&t) const noexcept {
		const auto i = prometheus_exporters.find(std::forward<T>(t));
		return i != prometheus_exporters.end()
			? &i->second
			: nullptr;
	}

#ifdef HAVE_AVAHI
	template<typename T>
	[[gnu::pure]]
	const LbPrometheusDiscoveryConfig *FindPrometheusDiscovery(T &&t) const noexcept {
		const auto i = prometheus_discoveries.find(std::forward<T>(t));
		return i != prometheus_discoveries.end()
			? &i->second
			: nullptr;
	}
#endif

	template<typename T>
	[[gnu::pure]]
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

	[[gnu::pure]]
	bool HasZeroConf() const noexcept {
#ifdef HAVE_AVAHI
		for (const auto &i : listeners)
			if (i.HasZeroConf())
				return true;
#endif

		return false;
	}

	[[gnu::pure]]
	bool HasTransparentSource() const noexcept {
		for (const auto &i : clusters)
			if (i.second.transparent_source)
				return true;

		return false;
	}

	[[gnu::pure]]
	bool HasPrometheusExporter() const noexcept {
		return !prometheus_exporters.empty();
	}

	void HandleSet(std::string_view name, const char *value);
};

/**
 * Load and parse the specified configuration file.  Throws an
 * exception on error.
 */
void
LoadConfigFile(LbConfig &config, const char *path);
