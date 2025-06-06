// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "config.h"
#include "Context.hxx"

#ifdef HAVE_LUA
#include "LuaInitHook.hxx"
#endif

#include <cstddef>
#include <map>

struct CacheStats;
struct LbConfig;
struct LbGoto;
struct LbGotoConfig;
struct LbClusterConfig;
struct LbBranchConfig;
struct LbTranslationHandlerConfig;
struct LbPrometheusExporterConfig;
struct LbPrometheusDiscoveryConfig;
struct LbLuaHandlerConfig;
struct TranslationInvalidateRequest;
class EventLoop;
class LbCluster;
class LbBranch;
class LbTranslationHandler;
class LbPrometheusExporter;
class LbPrometheusDiscovery;
class LbLuaHandler;
struct LbInstance;

class LbGotoMap final : LbContext {
	const LbConfig &root_config;
	EventLoop &event_loop;

#ifdef HAVE_LUA
	LbLuaInitHook lua_init_hook{this};
#endif

	std::map<const LbClusterConfig *, LbCluster> clusters;
	std::map<const LbBranchConfig *, LbBranch> branches;
	std::map<const LbTranslationHandlerConfig *,
		 LbTranslationHandler> translation_handlers;
	std::map<const LbPrometheusExporterConfig *,
		 LbPrometheusExporter> prometheus_exporters;
#ifdef HAVE_AVAHI
	std::map<const LbPrometheusDiscoveryConfig *,
		 LbPrometheusDiscovery> prometheus_discoveries;
#endif

#ifdef HAVE_LUA
	std::map<const LbLuaHandlerConfig *,
		 LbLuaHandler> lua_handlers;
#endif

public:
	LbGotoMap(const LbConfig &_config,
		  LbContext _context,
		  EventLoop &_event_loop) noexcept;

	~LbGotoMap() noexcept;

	LbGotoMap(const LbGotoMap &) = delete;
	LbGotoMap &operator=(const LbGotoMap &) = delete;

	void Clear() noexcept;

	void FlushCaches() noexcept;
	void InvalidateTranslationCaches(const TranslationInvalidateRequest &request) noexcept;

	[[gnu::pure]]
	CacheStats GetTranslationCacheStats() const noexcept;

	LbGoto GetInstance(const char *name);
	LbGoto GetInstance(const LbGotoConfig &config);

	LbCluster &GetInstance(const LbClusterConfig &config);

	void SetInstance(LbInstance &instance) noexcept;

private:
	LbBranch &GetInstance(const LbBranchConfig &config);
#ifdef HAVE_LUA
	LbLuaHandler &GetInstance(const LbLuaHandlerConfig &config);
#endif
	LbTranslationHandler &GetInstance(const LbTranslationHandlerConfig &config);
	LbPrometheusExporter &GetInstance(const LbPrometheusExporterConfig &config);
#ifdef HAVE_AVAHI
	LbPrometheusDiscovery &GetInstance(const LbPrometheusDiscoveryConfig &config);
#endif
};
