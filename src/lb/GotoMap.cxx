// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "GotoMap.hxx"
#include "Goto.hxx"
#include "Cluster.hxx"
#include "Branch.hxx"
#include "TranslationHandler.hxx"
#include "PrometheusExporter.hxx"
#include "Config.hxx"
#include "MonitorManager.hxx"
#include "stats/CacheStats.hxx"

#ifdef HAVE_LUA
#include "LuaHandler.hxx"
#endif

#ifdef HAVE_AVAHI
#include "PrometheusDiscovery.hxx"
#endif

LbGotoMap::LbGotoMap(const LbConfig &_config,
		     LbContext _context,
		     EventLoop &_event_loop) noexcept
	:LbContext(_context),
	 root_config(_config),
	 event_loop(_event_loop)
{
}

LbGotoMap::~LbGotoMap() noexcept = default;

void
LbGotoMap::Clear() noexcept
{
	translation_handlers.clear();
	prometheus_exporters.clear();
#ifdef HAVE_AVAHI
	prometheus_discoveries.clear();
#endif
	clusters.clear();
}

void
LbGotoMap::FlushCaches() noexcept
{
	for (auto &i : translation_handlers)
		i.second.FlushCache();
}

void
LbGotoMap::InvalidateTranslationCaches(const TranslationInvalidateRequest &request) noexcept
{
	for (auto &i : translation_handlers)
		i.second.InvalidateCache(request);
}

CacheStats
LbGotoMap::GetTranslationCacheStats() const noexcept
{
	CacheStats stats{};
	for (const auto &i : translation_handlers)
		stats += i.second.GetCacheStats();
	return stats;
}

LbGoto
LbGotoMap::GetInstance(const char *name)
{
	return GetInstance(root_config.FindGoto(name));
}

LbGoto
LbGotoMap::GetInstance(const LbGotoConfig &config)
{
	struct GetInstanceHelper {
		LbGotoMap &map;

		LbGoto operator()(std::monostate) const noexcept {
			return {};
		}

		LbGoto operator()(const LbClusterConfig *cluster) const {
			return map.GetInstance(*cluster);
		}

		LbGoto operator()(const LbBranchConfig *branch) const {
			return map.GetInstance(*branch);
		}

#ifdef HAVE_LUA
		LbGoto operator()(const LbLuaHandlerConfig *lua) const {
			return map.GetInstance(*lua);
		}
#endif // HAVE_LUA

		LbGoto operator()(const LbTranslationHandlerConfig *translation) const {
			return map.GetInstance(*translation);
		}

		LbGoto operator()(const LbPrometheusExporterConfig *exporter) const {
			return map.GetInstance(*exporter);
		}

#ifdef HAVE_AVAHI
		LbGoto operator()(const LbPrometheusDiscoveryConfig *discovery) const {
			return map.GetInstance(*discovery);
		}
#endif // HAVE_AVAHI

		LbGoto operator()(const LbSimpleHttpResponse &response) const noexcept {
			return response;
		}
	};

	return std::visit(GetInstanceHelper{*this}, config.destination);
}

LbCluster &
LbGotoMap::GetInstance(const LbClusterConfig &config)
{
	const LbContext &context = *this;

	auto *monitor_stock = config.monitor != nullptr
		? &monitors[*config.monitor]
		: nullptr;

	return clusters.try_emplace(&config,
				    config, context, monitor_stock)
		.first->second;
}

LbBranch &
LbGotoMap::GetInstance(const LbBranchConfig &config)
{
	return branches.try_emplace(&config, *this, config)
		.first->second;
}

#ifdef HAVE_LUA

LbLuaHandler &
LbGotoMap::GetInstance(const LbLuaHandlerConfig &config)
{
	return lua_handlers.try_emplace(&config, event_loop, lua_init_hook, config)
		.first->second;
}

#endif // HAVE_LUA

LbTranslationHandler &
LbGotoMap::GetInstance(const LbTranslationHandlerConfig &config)
{
	return translation_handlers.try_emplace(&config,
						event_loop, *this, config)
		.first->second;
}

LbPrometheusExporter &
LbGotoMap::GetInstance(const LbPrometheusExporterConfig &config)
{
	return prometheus_exporters.try_emplace(&config, config)
		.first->second;
}

#ifdef HAVE_AVAHI

LbPrometheusDiscovery &
LbGotoMap::GetInstance(const LbPrometheusDiscoveryConfig &config)
{
	const LbContext &context = *this;

	return prometheus_discoveries.try_emplace(&config, config, context)
		.first->second;
}

#endif

void
LbGotoMap::SetInstance(LbInstance &instance) noexcept
{
	for (auto &[name, exporter] : prometheus_exporters)
		exporter.SetInstance(instance);
}
