// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "Context.hxx"
#include "LuaInitHook.hxx"

#include <cstddef>
#include <map>

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

	LbLuaInitHook lua_init_hook;

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
	std::map<const LbLuaHandlerConfig *,
		 LbLuaHandler> lua_handlers;

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
	std::size_t GetAllocatedTranslationCacheMemory() const noexcept;

	LbGoto GetInstance(const char *name);
	LbGoto GetInstance(const LbGotoConfig &config);

	LbCluster &GetInstance(const LbClusterConfig &config);

	void SetInstance(LbInstance &instance) noexcept;

private:
	LbBranch &GetInstance(const LbBranchConfig &config);
	LbLuaHandler &GetInstance(const LbLuaHandlerConfig &config);
	LbTranslationHandler &GetInstance(const LbTranslationHandlerConfig &config);
	LbPrometheusExporter &GetInstance(const LbPrometheusExporterConfig &config);
#ifdef HAVE_AVAHI
	LbPrometheusDiscovery &GetInstance(const LbPrometheusDiscoveryConfig &config);
#endif
};
