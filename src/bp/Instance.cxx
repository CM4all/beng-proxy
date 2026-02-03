// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Instance.hxx"
#include "Listener.hxx"
#include "LStats.hxx"
#include "Connection.hxx"
#include "PerSite.hxx"
#include "LSSHandler.hxx"
#include "memory/fb_pool.hxx"
#include "event/net/control/Server.hxx"
#include "cluster/TcpBalancer.hxx"
#include "pipe/Stock.hxx"
#include "http/rl/DirectResourceLoader.hxx"
#include "http/rl/CachedResourceLoader.hxx"
#include "http/rl/FilterResourceLoader.hxx"
#include "http/rl/BufferedResourceLoader.hxx"
#include "http/cache/EncodingCache.hxx"
#include "http/cache/FilterCache.hxx"
#include "http/cache/Public.hxx"
#include "translation/Stock.hxx"
#include "translation/Cache.hxx"
#include "translation/Multi.hxx"
#include "translation/Builder.hxx"
#include "widget/Registry.hxx"
#include "http/local/Stock.hxx"
#include "fcgi/Stock.hxx"
#include "was/Stock.hxx"
#include "was/MStock.hxx"
#include "was/RStock.hxx"
#include "tcp_stock.hxx"
#include "ssl/Client.hxx"
#include "fs/Stock.hxx"
#include "fs/Balancer.hxx"
#include "nghttp2/Stock.hxx"
#include "stock/MapStock.hxx"
#include "session/Manager.hxx"
#include "session/Save.hxx"
#include "spawn/CgroupMemoryThrottle.hxx"
#include "spawn/CgroupMultiWatch.hxx"
#include "spawn/CgroupPidsThrottle.hxx"
#include "spawn/Client.hxx"
#include "spawn/Launch.hxx"
#include "net/ListenStreamStock.hxx"
#include "access_log/Glue.hxx"
#include "time/Cast.hxx" // for ToFloatSeconds()
#include "util/PrintException.hxx"

#ifdef HAVE_AVAHI
#include "lib/avahi/Client.hxx"
#include "lib/avahi/Publisher.hxx"
#endif

#include <fmt/core.h>

#include <sys/signal.h>

static constexpr auto COMPRESS_INTERVAL = std::chrono::minutes(10);

#ifdef HAVE_LIBSYSTEMD

static constexpr uint_least64_t
GetMemoryLimit(const SystemdUnitProperties &properties) noexcept
{
	return properties.memory_high > 0
		? properties.memory_high
		: properties.memory_max;
}

#endif

BpInstance::BpInstance(BpConfig &&_config,
		       LaunchSpawnServerResult &&spawner) noexcept
	:config(std::move(_config)),
	 shutdown_listener(event_loop, BIND_THIS_METHOD(ShutdownCallback)),
	 sighup_event(event_loop, SIGHUP, BIND_THIS_METHOD(ReloadEventCallback)),
	 compress_timer(event_loop, BIND_THIS_METHOD(OnCompressTimer)),
	 spawn(spawner.socket.IsDefined()
	       ? std::make_unique<SpawnServerClient>(event_loop,
						     config.spawn, std::move(spawner.socket),
						     spawner.cgroup.IsDefined(),
						     true)
	       : nullptr),
#ifdef HAVE_LIBSYSTEMD
	 cgroup_multi_watch(spawn && spawner.cgroup.IsDefined() ? new CgroupMultiWatch(event_loop) : nullptr),
	 cgroup_memory_throttle(spawn && spawner.cgroup.IsDefined() &&
				config.spawn.systemd_scope_properties.HaveMemoryLimit()
				? std::make_unique<CgroupMemoryThrottle>(event_loop,
									 spawner.cgroup,
									 *spawn,
									 BIND_THIS_METHOD(HandleMemoryWarning),
									 GetMemoryLimit(config.spawn.systemd_scope_properties))
				: nullptr),
	 spawn_service(cgroup_memory_throttle ? static_cast<SpawnService *>(cgroup_memory_throttle.get()) : spawn.get()),
#else
	 spawn_service(spawn.get()),
#endif
	 session_save_timer(event_loop, BIND_THIS_METHOD(SaveSessions))
{
#ifdef HAVE_LIBSYSTEMD
	if (spawn && spawner.cgroup.IsDefined()) {
		if (config.spawn.systemd_scope_properties.tasks_max > 0) {
			cgroup_pids_throttle = std::make_unique<CgroupPidsThrottle>(event_loop, spawner.cgroup,
										    *spawn_service,
										    BIND_THIS_METHOD(HandleMemoryWarning),
										    config.spawn.systemd_scope_properties.tasks_max);
			spawn_service = cgroup_pids_throttle.get();
		}
	}
#endif

	if (config.populate_io_buffers)
		request_slice_pool.Populate();

	ScheduleCompress();
}

BpInstance::~BpInstance() noexcept
{
	delete (BufferedResourceLoader *)buffered_filter_resource_loader;

	if (filter_resource_loader != direct_resource_loader)
		delete (FilterResourceLoader *)filter_resource_loader;

	delete (DirectResourceLoader *)direct_resource_loader;

	FreeStocksAndCaches();
}

void
BpInstance::FreeStocksAndCaches() noexcept
{
	delete std::exchange(widget_registry, nullptr);
	translation_service.reset();
	cached_translation_service.reset();
	translation_caches.reset();
	uncached_translation_service.reset();
	translation_clients.reset();

	if (http_cache != nullptr) {
		delete (CachedResourceLoader *)cached_resource_loader;
		cached_resource_loader = nullptr;

		http_cache_close(http_cache);
		http_cache = nullptr;
	}

	if (filter_cache != nullptr) {
		filter_cache_close(filter_cache);
		filter_cache = nullptr;
	}

	encoding_cache.reset();

	lhttp_stock.reset();
	fcgi_stock.reset();

#ifdef HAVE_LIBWAS
	delete std::exchange(was_stock, nullptr);
	delete std::exchange(multi_was_stock, nullptr);
	delete std::exchange(remote_was_stock, nullptr);
#endif

	listen_stream_stock.reset();
	spawn_listen_stream_stock_handler.reset();

	delete std::exchange(fs_balancer, nullptr);
	delete std::exchange(fs_stock, nullptr);
#ifdef HAVE_NGHTTP2
	delete std::exchange(nghttp2_stock, nullptr);
#endif
	ssl_client_factory.reset();

	delete std::exchange(tcp_balancer, nullptr);

	delete tcp_stock;
	tcp_stock = nullptr;

	delete std::exchange(pipe_stock, nullptr);
}

void
BpInstance::ForkCow(bool inherit) noexcept
{
	fb_pool_fork_cow(inherit);

	if (translation_caches)
		translation_caches->ForkCow(inherit);

	if (http_cache != nullptr)
		http_cache_fork_cow(*http_cache, inherit);

	if (filter_cache != nullptr)
		filter_cache_fork_cow(*filter_cache, inherit);

	if (encoding_cache)
		encoding_cache->ForkCow(inherit);
}

void
BpInstance::ApplyPopulate() noexcept
{
	if (config.populate_translate_cache && translation_caches)
		translation_caches->Populate();

	if (config.populate_http_cache && http_cache != nullptr)
		http_cache_populate(*http_cache);

	if (config.populate_filter_cache && filter_cache != nullptr)
		filter_cache_populate(*filter_cache);

	if (config.populate_encoding_cache && encoding_cache)
		encoding_cache->Populate();
}

void
BpInstance::Compress() noexcept
{
	fb_pool_compress();
	request_slice_pool.Compress();

	if (per_site)
		per_site->Expire(ToFloatSeconds(event_loop.SteadyNow().time_since_epoch()));
}

void
BpInstance::ScheduleCompress() noexcept
{
	compress_timer.Schedule(COMPRESS_INTERVAL);
}

void
BpInstance::OnCompressTimer() noexcept
{
	Compress();
	ScheduleCompress();
}

void
BpInstance::FadeChildren() noexcept
{
	if (lhttp_stock != nullptr)
		lhttp_stock->FadeAll();

	if (fcgi_stock != nullptr)
		fcgi_stock->FadeAll();

#ifdef HAVE_LIBWAS
	if (was_stock != nullptr)
		was_stock->FadeAll();
	if (multi_was_stock != nullptr)
		multi_was_stock->FadeAll();
#endif

	if (listen_stream_stock)
		listen_stream_stock->FadeAll();
}

void
BpInstance::FadeTaggedChildren(std::string_view tag) noexcept
{
	if (lhttp_stock != nullptr)
		lhttp_stock->FadeTag(tag);

	if (fcgi_stock != nullptr)
		fcgi_stock->FadeTag(tag);

#ifdef HAVE_LIBWAS
	if (was_stock != nullptr)
		was_stock->FadeTag(tag);
	if (multi_was_stock != nullptr)
		multi_was_stock->FadeTag(tag);
#endif

	if (listen_stream_stock)
		listen_stream_stock->FadeTag(tag);
}

void
BpInstance::FlushTranslationCaches() noexcept
{
	if (widget_registry != nullptr)
		widget_registry->FlushCache();

	if (translation_caches)
		translation_caches->Flush();
}

void
BpInstance::ReloadState() noexcept
{
#ifdef HAVE_AVAHI
	for (auto &i : listeners) {
		const auto name = i.GetStateName();
		if (name.empty())
			continue;

		if (i.HasZeroconf()) {
			const auto path = fmt::format("beng-proxy/listener/{}/zeroconf", name);
			i.SetZeroconfVisible(state_directories.GetBool(path.c_str(), true));
		}
	}
#endif // HAVE_AVAHI
}

#ifdef HAVE_LIBSYSTEMD

void
BpInstance::HandleMemoryWarning() noexcept
{
	std::size_t n = 0;

	if (lhttp_stock != nullptr)
		n += lhttp_stock->DiscardSome();

#ifdef HAVE_LIBWAS
	if (multi_was_stock != nullptr)
		n += multi_was_stock->DiscardSome();
#endif

	if (n > 0)
		fmt::print(stderr, "Discarded {} child processes\n", n);
}

#endif // HAVE_LIBSYSTEMD

#ifdef HAVE_LIBWAS

void
BpInstance::OnWasMetric(std::string_view name, float value) noexcept
{
	was_metrics[std::string{name}] += value;
}

#endif

bool
BpInstance::OnAvahiError(std::exception_ptr e) noexcept
{
	PrintException(e);
	return true;
}

void
BpInstance::SaveSessions() noexcept
{
	session_save(*session_manager);

	ScheduleSaveSessions();
}

void
BpInstance::ScheduleSaveSessions() noexcept
{
	/* save all sessions every 2 minutes */
	session_save_timer.Schedule(std::chrono::minutes(2));
}

SharedLeasePtr<BpPerSite>
BpInstance::MakePerSite(std::string_view site) noexcept
{
	if (!per_site)
		per_site = std::make_unique<BpPerSiteMap>();

	return per_site->Make(StringWithHash{site});
}
