// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "CommandLine.hxx"
#include "Instance.hxx"
#include "Listener.hxx"
#include "LStats.hxx"
#include "Connection.hxx"
#include "Global.hxx"
#include "LSSHandler.hxx"
#include "pool/pool.hxx"
#include "memory/fb_pool.hxx"
#include "session/Manager.hxx"
#include "session/Save.hxx"
#include "tcp_stock.hxx"
#include "translation/Stock.hxx"
#include "translation/Cache.hxx"
#include "translation/Multi.hxx"
#include "translation/Builder.hxx"
#include "cluster/TcpBalancer.hxx"
#include "fs/Stock.hxx"
#include "fs/Balancer.hxx"
#include "nghttp2/Stock.hxx"
#include "http/cache/EncodingCache.hxx"
#include "http/cache/FilterCache.hxx"
#include "http/cache/Public.hxx"
#include "http/local/Stock.hxx"
#include "fcgi/Stock.hxx"
#include "was/Stock.hxx"
#include "was/MStock.hxx"
#include "was/RStock.hxx"
#include "delegate/Stock.hxx"
#include "thread/Pool.hxx"
#include "pipe/Stock.hxx"
#include "DirectResourceLoader.hxx"
#include "CachedResourceLoader.hxx"
#include "FilterResourceLoader.hxx"
#include "BufferedResourceLoader.hxx"
#include "bp/Control.hxx"
#include "widget/Registry.hxx"
#include "access_log/Glue.hxx"
#include "ssl/Init.hxx"
#include "ssl/Client.hxx"
#include "system/KernelVersion.hxx"
#include "system/SetupProcess.hxx"
#include "system/ProcessName.hxx"
#include "spawn/CgroupWatch.hxx"
#include "spawn/Launch.hxx"
#include "spawn/Client.hxx"
#include "net/ListenStreamStock.hxx"
#include "net/SocketAddress.hxx"
#include "net/StaticSocketAddress.hxx"
#include "io/Logger.hxx"
#include "io/SpliceSupport.hxx"
#include "util/StringCompare.hxx"
#include "util/PrintException.hxx"

#ifdef HAVE_LIBCAP
#include "lib/cap/Glue.hxx"
#include "system/Capabilities.hxx"
#endif // HAVE_LIBCAP

#if defined(HAVE_LIBSYSTEMD) || defined(HAVE_AVAHI)
#include "lib/dbus/Init.hxx"
#include "lib/dbus/Connection.hxx"
#endif

#ifdef HAVE_LIBSYSTEMD
#include <systemd/sd-daemon.h>
#endif

#ifdef HAVE_AVAHI
#include "lib/avahi/Client.hxx"
#include "lib/avahi/Publisher.hxx"
#include "lib/avahi/Service.hxx"
#endif

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#ifndef NDEBUG
bool debug_mode = false;
#endif

inline TranslationServiceBuilder &
BpInstance::GetTranslationServiceBuilder() const noexcept
{
	return translation_caches
		? (TranslationServiceBuilder &)*translation_caches
		: *translation_stocks;
}

#ifdef HAVE_AVAHI

inline Avahi::Client &
BpInstance::GetAvahiClient()
{
	if (!avahi_client) {
		Avahi::ErrorHandler &error_handler = *this;
		avahi_client = std::make_unique<Avahi::Client>(event_loop,
							       error_handler);
	}

	return *avahi_client;
}

Avahi::Publisher &
BpInstance::GetAvahiPublisher()
{
	if (!avahi_publisher) {
		Avahi::ErrorHandler &error_handler = *this;
		avahi_publisher = std::make_unique<Avahi::Publisher>(GetAvahiClient(),
								     "beng-proxy",
								     error_handler);
	}

	return *avahi_publisher;
}

#endif

void
BpInstance::ShutdownCallback() noexcept
{
	uring.SetVolatile();
	fd_cache.Disable();

	DisableSignals();
	thread_pool_stop();

	spawn->Shutdown();

#ifdef HAVE_LIBSYSTEMD
	cgroup_memory_watch.reset();
	memory_warning_timer.Cancel();
#endif

	listeners.clear();

	pool_commit();

#ifdef HAVE_AVAHI
	avahi_publisher.reset();
	avahi_client.reset();
#endif

	compress_timer.Cancel();

	zombie_reaper.Disable();

	thread_pool_join();

	background_manager.AbortAll();

	session_save_timer.Cancel();
	session_save_deinit(*session_manager);

	session_manager.reset();

	FreeStocksAndCaches();

	global_control_handler_deinit(this);

	pool_commit();
}

void
BpInstance::ReloadEventCallback(int) noexcept
{
	LogConcat(3, "main", "caught SIGHUP, flushing all caches (pid=",
		  (int)getpid(), ")");

	FadeChildren();

	FlushTranslationCaches();

	if (http_cache != nullptr)
		http_cache_flush(*http_cache);

	if (filter_cache != nullptr)
		filter_cache_flush(*filter_cache);

	if (encoding_cache)
		encoding_cache->Flush();

#ifdef HAVE_NGHTTP2
	if (nghttp2_stock != nullptr)
		nghttp2_stock->FadeAll();
#endif

#ifdef HAVE_LIBWAS
	if (remote_was_stock != nullptr)
		remote_was_stock->FadeAll();
#endif

	if (listen_stream_stock)
		listen_stream_stock->FadeAll();

	fd_cache.Flush();

	Compress();

	ReloadState();
}

void
BpInstance::EnableSignals() noexcept
{
	shutdown_listener.Enable();
	sighup_event.Enable();
}

void
BpInstance::DisableSignals() noexcept
{
	shutdown_listener.Disable();
	sighup_event.Disable();
}

static std::shared_ptr<TranslationService>
MakeTranslationService(EventLoop &event_loop, TranslationServiceBuilder &b,
		       const std::forward_list<LocalSocketAddress> &l)
{
	auto multi = std::make_shared<MultiTranslationService>();
	for (const SocketAddress a : l)
		multi->Add(b.Get(a, event_loop));

	return multi;
}

inline SocketDescriptor
BpInstance::GetChildLogSocket(const UidGid *logger_user)
{
	if (config.child_error_log.type != AccessLogConfig::Type::INTERNAL) {
		if (!child_error_log)
			child_error_log.reset(AccessLogGlue::Create(config.child_error_log,
								    logger_user));

		if (child_error_log)
			return child_error_log->GetChildSocket();
	}

	if (auto *access_logger = access_log.Make(config.access_log, logger_user, {}))
		return access_logger->GetChildSocket();

	return SocketDescriptor::Undefined();
}

void
BpInstance::AddListener(const BpListenerConfig &c, const UidGid *logger_user)
{
	auto ts = c.translation_sockets.empty()
		? translation_service
		: MakeTranslationService(event_loop,
					 GetTranslationServiceBuilder(),
					 c.translation_sockets);

	listeners.emplace_front(*this,
				listener_stats[c.tag],
				config.access_log.FindXForwardedForConfig(c.access_logger_name),
				access_log.Make(config.access_log, logger_user,
						c.access_logger_name),
				std::move(ts),
				c, c.Create(SOCK_STREAM));
}

[[gnu::const]]
static unsigned
GetDefaultPort() noexcept
{
#ifndef NDEBUG
#ifdef HAVE_LIBCAP
	if (!HaveNetBindService())
		return 8080;
#endif
#endif

	return 80;
}

int main(int argc, char **argv)
try {
	if (!IsKernelVersionOrNewer({5, 12}))
		throw "Your Linux kernel is too old; this program requires at least 5.12";

	if (geteuid() == 0)
		throw "Refusing to run as root";

	InitProcessName(argc, argv);

#ifndef NDEBUG
#ifdef HAVE_LIBCAP
	debug_mode = !HaveSetuid();
#endif
#endif

	/* configuration */
	BpCmdLine cmdline;
	BpConfig _config;
	ParseCommandLine(cmdline, _config, argc, argv);

	if (cmdline.config_file != nullptr)
		LoadConfigFile(_config, cmdline.config_file);

	_config.Finish(GetDefaultPort());

	/* initialize */

	SetupProcess();

	auto spawner = LaunchSpawnServer(_config.spawn, nullptr);

#if defined(HAVE_LIBSYSTEMD) || defined(HAVE_AVAHI)
	const ODBus::ScopeInit dbus_init;
	dbus_connection_set_exit_on_disconnect(ODBus::Connection::GetSystem(),
					       false);
#endif

	const ScopeFbPoolInit fb_pool_init;

	BpInstance instance{
		std::move(_config),
		std::move(spawner),
	};

#ifdef HAVE_LIBCAP
	capabilities_init();
#endif // HAVE_LIBCAP

	const ScopeSslGlobalInit ssl_init;
	instance.ssl_client_factory =
		std::make_unique<SslClientFactory>(instance.config.ssl_client);

	direct_global_init();

	instance.EnableSignals();

	global_control_handler_init(&instance);

	spawner = {}; // close the pidfd

	instance.session_manager =
		std::make_unique<SessionManager>(instance.event_loop,
						 instance.config.session_idle_timeout,
						 instance.config.cluster_size,
						 instance.config.cluster_node);

	if (!instance.config.session_save_path.empty()) {
		session_save_init(*instance.session_manager,
				  instance.config.session_save_path.c_str());
		instance.ScheduleSaveSessions();
	}

	/* launch the access logger */

	const auto child_log_socket = instance.GetChildLogSocket(&cmdline.logger_user);

	const auto &child_log_options = instance.config.child_error_log.type != AccessLogConfig::Type::INTERNAL
		? instance.config.child_error_log.child_error_options
		: instance.config.access_log.main.child_error_options;

	/* initialize ResourceLoader and all its dependencies */

	instance.tcp_stock = new TcpStock(instance.event_loop,
					  instance.config.tcp_stock_limit,
					  instance.config.tcp_stock_max_idle);
	instance.tcp_balancer = new TcpBalancer(*instance.tcp_stock,
						instance.failure_manager);

	instance.fs_stock = new FilteredSocketStock(instance.event_loop,
						    instance.config.tcp_stock_limit,
						    instance.config.tcp_stock_max_idle);
	instance.fs_balancer = new FilteredSocketBalancer(*instance.fs_stock,
							  instance.failure_manager);

#ifdef HAVE_NGHTTP2
	instance.nghttp2_stock = new NgHttp2::Stock();
#endif

	assert(!instance.config.translation_sockets.empty());

	instance.translation_stocks =
		std::make_unique<TranslationStockBuilder>(instance.config.translate_stock_limit);
	instance.uncached_translation_service =
		std::make_unique<MultiTranslationService>();

	if (instance.config.translate_cache_size > 0) {
		instance.translation_caches =
			std::make_unique<TranslationCacheBuilder>(*instance.translation_stocks,
								  instance.root_pool,
								  instance.config.translate_cache_size);
		instance.cached_translation_service =
			std::make_unique<MultiTranslationService>();
	}

	for (const auto &config : instance.config.translation_sockets) {
		instance.uncached_translation_service
			->Add(instance.translation_stocks->Get(config,
							       instance.event_loop));

		if (instance.config.translate_cache_size > 0)
			instance.cached_translation_service
				->Add(instance.translation_caches->Get(config,
								       instance.event_loop));
	}

	instance.translation_service = instance.config.translate_cache_size > 0
		? instance.cached_translation_service
		: instance.uncached_translation_service;


	/* the WidgetRegistry class has its own cache and doesn't need
	   the TranslationCache */
	instance.widget_registry =
		new WidgetRegistry(instance.root_pool,
				   *instance.uncached_translation_service);

	if (instance.translation_service != nullptr) {
		instance.spawn_listen_stream_stock_handler =
			std::make_unique<BpListenStreamStockHandler>(instance);
		instance.listen_stream_stock = std::make_unique<ListenStreamStock>(instance.event_loop,
										   *instance.spawn_listen_stream_stock_handler);
	}


	instance.lhttp_stock = lhttp_stock_new(instance.config.lhttp_stock_limit,
					       instance.config.lhttp_stock_max_idle,
					       instance.event_loop,
					       *instance.spawn_service,
					       instance.listen_stream_stock.get(),
					       child_log_socket,
					       child_log_options);

	instance.fcgi_stock = fcgi_stock_new(instance.config.fcgi_stock_limit,
					     instance.config.fcgi_stock_max_idle,
					     instance.event_loop,
					     *instance.spawn_service,
					     instance.listen_stream_stock.get(),
					     child_log_socket, child_log_options);

#ifdef HAVE_LIBWAS
	instance.was_stock = new WasStock(instance.event_loop,
					  *instance.spawn_service,
					  instance.listen_stream_stock.get(),
					  child_log_socket, child_log_options,
					  instance.config.was_stock_limit,
					  instance.config.was_stock_max_idle);
	instance.multi_was_stock =
		new MultiWasStock(instance.config.multi_was_stock_limit,
				  instance.config.multi_was_stock_max_idle,
				  instance.event_loop,
				  *instance.spawn_service,
				  child_log_socket,
				  child_log_options);
	instance.remote_was_stock =
		new RemoteWasStock(instance.config.remote_was_stock_limit,
				   instance.config.remote_was_stock_max_idle,
				   instance.event_loop);
#endif

	instance.delegate_stock = delegate_stock_new(instance.event_loop,
						     *instance.spawn_service);

	instance.direct_resource_loader =
		new DirectResourceLoader(instance.event_loop,
#ifdef HAVE_URING
					 instance.uring.get(),
#endif
					 instance.tcp_balancer,
					 *instance.fs_balancer,
#ifdef HAVE_NGHTTP2
					 *instance.nghttp2_stock,
#endif
					 *instance.spawn_service,
					 instance.lhttp_stock,
					 instance.fcgi_stock,
#ifdef HAVE_LIBWAS
					 instance.was_stock,
					 instance.multi_was_stock,
					 instance.remote_was_stock,
					 &instance,
#endif
					 instance.delegate_stock,
					 instance.ssl_client_factory.get(),

					 /* TODO how to support
					    per-listener XFF
					    setting? */
					 instance.config.access_log.main.xff);

	if (instance.config.http_cache_size > 0) {
		instance.http_cache = http_cache_new(instance.root_pool,
						     instance.config.http_cache_size,
						     instance.config.http_cache_obey_no_cache,
						     instance.event_loop,
						     *instance.direct_resource_loader);

		instance.cached_resource_loader =
			new CachedResourceLoader(*instance.http_cache);
	} else
		instance.cached_resource_loader = instance.direct_resource_loader;

	instance.pipe_stock = new PipeStock(instance.event_loop);

	if (instance.config.filter_cache_size > 0) {
		instance.filter_cache = filter_cache_new(instance.root_pool,
							 instance.config.filter_cache_size,
							 instance.event_loop,
							 *instance.direct_resource_loader);
		instance.filter_resource_loader =
			new FilterResourceLoader(*instance.filter_cache);
	} else
		instance.filter_resource_loader = instance.direct_resource_loader;

	if (instance.config.encoding_cache_size > 0)
		instance.encoding_cache = std::make_unique<EncodingCache>(instance.event_loop,
									  instance.config.encoding_cache_size);

	instance.buffered_filter_resource_loader =
		new BufferedResourceLoader(instance.event_loop,
					   *instance.filter_resource_loader,
					   instance.pipe_stock);

	global_translation_service = instance.translation_service.get();
	global_pipe_stock = instance.pipe_stock;

	if (cmdline.debug_listener_tag == nullptr) {
		for (const auto &i : instance.config.listen)
			instance.AddListener(i, &cmdline.logger_user);
	} else {
		BpListenerConfig config;
		if (!StringIsEmpty(cmdline.debug_listener_tag))
			config.tag = cmdline.debug_listener_tag;

		instance.listeners.emplace_front(instance,
						 instance.listener_stats[cmdline.debug_listener_tag],
						 instance.config.access_log.FindXForwardedForConfig({}),
						 instance.access_log.Make(instance.config.access_log,
									  &cmdline.logger_user,
									  {}),
						 instance.translation_service,
						 config,
						 UniqueSocketDescriptor{STDIN_FILENO});
	}

	/* daemonize II */

#ifdef HAVE_LIBCAP
	try {
		capabilities_post_setuid({});
	} catch (...) {
		/* if we failed to preserve CAP_NET_BIND_SERVICE, drop
		   all capabilities */
		capabilities_post_setuid({});
	}
#endif // HAVE_LIBCAP

	instance.ReloadState();

#ifdef HAVE_LIBSYSTEMD
	/* tell systemd we're ready */
	sd_notify(0, "READY=1");
#endif

	/* main loop */

	instance.event_loop.Run();

	/* cleanup */

	thread_pool_deinit();
} catch (...) {
	PrintException(std::current_exception());
	return EXIT_FAILURE;
}
