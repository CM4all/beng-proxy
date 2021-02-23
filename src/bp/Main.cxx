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

#include "CommandLine.hxx"
#include "Instance.hxx"
#include "Listener.hxx"
#include "Connection.hxx"
#include "Global.hxx"
#include "pool/pool.hxx"
#include "fb_pool.hxx"
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
#include "http_cache.hxx"
#include "lhttp_stock.hxx"
#include "fcgi/Stock.hxx"
#include "was/Stock.hxx"
#include "delegate/Stock.hxx"
#include "fcache.hxx"
#include "thread/Pool.hxx"
#include "pipe_stock.hxx"
#include "nfs/Stock.hxx"
#include "nfs/Cache.hxx"
#include "DirectResourceLoader.hxx"
#include "CachedResourceLoader.hxx"
#include "FilterResourceLoader.hxx"
#include "BufferedResourceLoader.hxx"
#include "bp/Control.hxx"
#include "widget/Registry.hxx"
#include "access_log/Glue.hxx"
#include "ssl/Init.hxx"
#include "ssl/Client.hxx"
#include "system/CapabilityGlue.hxx"
#include "system/KernelVersion.hxx"
#include "system/SetupProcess.hxx"
#include "system/ProcessName.hxx"
#include "capabilities.hxx"
#include "spawn/Glue.hxx"
#include "spawn/Client.hxx"
#include "net/SocketAddress.hxx"
#include "net/StaticSocketAddress.hxx"
#include "io/Logger.hxx"
#include "io/SpliceSupport.hxx"
#include "util/PrintException.hxx"
#include "random.hxx"

#ifdef HAVE_URING
#include "event/uring/Manager.hxx"
#endif

#if defined(HAVE_LIBSYSTEMD) || defined(HAVE_AVAHI)
#include "odbus/Init.hxx"
#include "odbus/Connection.hxx"
#endif

#ifdef HAVE_LIBSYSTEMD
#include <systemd/sd-daemon.h>
#endif

#ifdef HAVE_AVAHI
#include "avahi/Client.hxx"
#include "avahi/Publisher.hxx"
#endif

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef __linux
#include <sys/prctl.h>
#endif

#ifndef NDEBUG
bool debug_mode = false;
#endif

static constexpr cap_value_t cap_keep_list[] = {
	/* allow libnfs to bind to privileged ports, which in turn allows
	   disabling the "insecure" flag on the NFS server */
	CAP_NET_BIND_SERVICE,
};

inline TranslationServiceBuilder &
BpInstance::GetTranslationServiceBuilder() const noexcept
{
	return translation_caches
		? (TranslationServiceBuilder &)*translation_caches
		: *translation_stocks;
}

void
BpInstance::EnableListeners() noexcept
{
	for (auto &listener : listeners)
		listener.AddEvent();
}

void
BpInstance::DisableListeners() noexcept
{
	for (auto &listener : listeners)
		listener.RemoveEvent();
}

void
BpInstance::ShutdownCallback() noexcept
{
#ifdef HAVE_URING
	if (uring)
		uring->SetVolatile();
#endif

	DisableSignals();
	thread_pool_stop();

	spawn->Shutdown();

	listeners.clear();

	connections.clear_and_dispose(BpConnection::Disposer());

	pool_commit();

#ifdef HAVE_AVAHI
	avahi_publisher.reset();
	avahi_client.reset();
#endif

	compress_timer.Cancel();

	child_process_registry.SetVolatile();

	thread_pool_join();

	background_manager.AbortAll();

	session_save_timer.Cancel();
	session_save_deinit(*session_manager);

	session_manager.reset();

	FreeStocksAndCaches();

	local_control_handler_deinit(this);
	global_control_handler_deinit(this);

	pool_commit();
}

void
BpInstance::ReloadEventCallback(int) noexcept
{
	LogConcat(3, "main", "caught SIGHUP, flushing all caches (pid=",
		  (int)getpid(), ")");

	unsigned n_ssl_sessions = FlushSSLSessionCache(LONG_MAX);
	LogConcat(3, "main", "flushed ", n_ssl_sessions, " SSL sessions");

	FadeChildren();

	FlushTranslationCaches();

	if (http_cache != nullptr)
		http_cache_flush(*http_cache);

	if (filter_cache != nullptr)
		filter_cache_flush(*filter_cache);

#ifdef HAVE_LIBNFS
	if (nfs_cache != nullptr)
		nfs_cache_flush(*nfs_cache);
#endif

#ifdef HAVE_NGHTTP2
	if (nghttp2_stock != nullptr)
		nghttp2_stock->FadeAll();
#endif

	Compress();
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
		       const std::forward_list<AllocatedSocketAddress> &l)
{
	auto multi = std::make_shared<MultiTranslationService>();
	for (const SocketAddress a : l)
		multi->Add(b.Get(a, event_loop));

	return multi;
}

void
BpInstance::AddListener(const BpConfig::Listener &c)
{
	auto ts = c.translation_sockets.empty()
		? translation_service
		: MakeTranslationService(event_loop,
					 GetTranslationServiceBuilder(),
					 c.translation_sockets);

	listeners.emplace_front(*this, std::move(ts),
				c.tag.empty() ? nullptr : c.tag.c_str(),
				c.auth_alt_host,
				c.ssl ? &c.ssl_config : nullptr);
	auto &listener = listeners.front();

	listener.Listen(c.Create(SOCK_STREAM));

#ifdef HAVE_AVAHI
	const char *const interface = c.interface.empty()
		? nullptr
		: c.interface.c_str();

	if (!c.zeroconf_service.empty()) {
		/* ask the kernel for the effective address via getsockname(),
		   because it may have changed, e.g. if the kernel has
		   selected a port for us */
		const auto local_address = listener.GetLocalAddress();
		if (local_address.IsDefined()) {
			if (!avahi_publisher) {
				if (!avahi_client)
					avahi_client = std::make_unique<Avahi::Client>(event_loop);
				avahi_publisher = std::make_unique<Avahi::Publisher>(*avahi_client,
										     "beng-proxy");
			}

			avahi_publisher->AddService(c.zeroconf_service.c_str(),
						    interface, local_address,
						    c.v6only);
		}
	}
#endif
}

int main(int argc, char **argv)
try {
	if (!IsKernelVersionOrNewer({4, 11}))
		throw "Your Linux kernel is too old; this program requires at least 4.11";

	if (geteuid() == 0)
		throw "Refusing to run as root";

	InitProcessName(argc, argv);

#ifndef NDEBUG
	debug_mode = !IsSysAdmin();
#endif

#if defined(HAVE_LIBSYSTEMD) || defined(HAVE_AVAHI)
	const ODBus::ScopeInit dbus_init;
	dbus_connection_set_exit_on_disconnect(ODBus::Connection::GetSystem(),
					       false);
#endif

	/* configuration */
	BpCmdLine cmdline;
	BpConfig _config;
	ParseCommandLine(cmdline, _config, argc, argv);

	if (cmdline.config_file != nullptr)
		LoadConfigFile(_config, cmdline.config_file);

	_config.Finish(debug_mode ? 8080 : 80);

	/* initialize */

	const ScopeFbPoolInit fb_pool_init;

	BpInstance instance(std::move(_config));

	SetupProcess();
	capabilities_init();

	const ScopeSslGlobalInit ssl_init;
	instance.ssl_client_factory =
		std::make_unique<SslClientFactory>(instance.config.ssl_client);

	direct_global_init();

#ifdef HAVE_URING
	try {
		instance.uring = std::make_unique<Uring::Manager>(instance.event_loop);
	} catch (...) {
		fprintf(stderr, "Failed to initialize io_uring: ");
		PrintException(std::current_exception());
	}
#endif

	instance.EnableSignals();

	global_control_handler_init(&instance);

	/* note: this function call passes a temporary SpawnConfig copy,
	   because the reference will be evaluated in the child process
	   after ~BpInstance() has been called */
	instance.spawn = StartSpawnServer(SpawnConfig(instance.config.spawn),
					  instance.child_process_registry,
					  nullptr,
					  [&instance](){
						  instance.event_loop.Reinit();

						  global_control_handler_deinit(&instance);
						  instance.listeners.clear();
						  instance.DisableSignals();

						  instance.~BpInstance();

#if defined(HAVE_LIBSYSTEMD) || defined(HAVE_AVAHI)
						  /* don't share the
						     DBus connection
						     with the
						     spawner */
						  dbus_shutdown();
#endif
					  });
	instance.spawn->SetHandler(instance);
	instance.spawn_service = instance.spawn.get();

	random_seed();
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

	local_control_handler_init(&instance);

	try {
		local_control_handler_open(&instance);
	} catch (const std::exception &e) {
		PrintException(e);
	}

	/* launch the access logger */

	instance.access_log.reset(AccessLogGlue::Create(instance.config.access_log,
							&cmdline.logger_user));

	if (instance.config.child_error_log.type != AccessLogConfig::Type::INTERNAL)
		instance.child_error_log.reset(AccessLogGlue::Create(instance.config.child_error_log,
								     &cmdline.logger_user));

	const auto child_log_socket = instance.child_error_log
		? instance.child_error_log->GetChildSocket()
		: (instance.access_log
		   ? instance.access_log->GetChildSocket()
		   : SocketDescriptor::Undefined());

	const auto &child_log_options = instance.config.child_error_log.type != AccessLogConfig::Type::INTERNAL
		? instance.config.child_error_log.child_error_options
		: instance.config.access_log.child_error_options;

	/* initialize ResourceLoader and all its dependencies */

	instance.tcp_stock = new TcpStock(instance.event_loop,
					  instance.config.tcp_stock_limit);
	instance.tcp_balancer = new TcpBalancer(*instance.tcp_stock,
						instance.failure_manager);

	instance.fs_stock = new FilteredSocketStock(instance.event_loop,
						    instance.config.tcp_stock_limit);
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

	instance.lhttp_stock = lhttp_stock_new(0, 8, instance.event_loop,
					       *instance.spawn_service,
					       child_log_socket,
					       child_log_options);

	instance.fcgi_stock = fcgi_stock_new(instance.config.fcgi_stock_limit,
					     instance.config.fcgi_stock_max_idle,
					     instance.event_loop,
					     *instance.spawn_service,
					     child_log_socket, child_log_options);

#ifdef HAVE_LIBWAS
	instance.was_stock = new WasStock(instance.event_loop,
					  *instance.spawn_service,
					  child_log_socket, child_log_options,
					  instance.config.was_stock_limit,
					  instance.config.was_stock_max_idle);
#endif

	instance.delegate_stock = delegate_stock_new(instance.event_loop,
						     *instance.spawn_service);

#ifdef HAVE_LIBNFS
	instance.nfs_stock = nfs_stock_new(instance.event_loop);
	instance.nfs_cache = nfs_cache_new(instance.root_pool,
					   instance.config.nfs_cache_size,
					   *instance.nfs_stock,
					   instance.event_loop);
#endif

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
#endif
					 instance.delegate_stock,
#ifdef HAVE_LIBNFS
					 instance.nfs_cache,
#endif
					 instance.ssl_client_factory.get());

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

	instance.buffered_filter_resource_loader =
		new BufferedResourceLoader(instance.event_loop,
					   *instance.filter_resource_loader,
					   instance.pipe_stock);

	global_translation_service = instance.translation_service.get();
	global_pipe_stock = instance.pipe_stock;

	if (cmdline.debug_listener_tag == nullptr) {
		for (const auto &i : instance.config.listen)
			instance.AddListener(i);
	} else {
		const char *tag = cmdline.debug_listener_tag;
		if (*tag == 0)
			tag = nullptr;

		instance.listeners.emplace_front(instance,
						 instance.translation_service,
						 tag,
						 false, nullptr);
		instance.listeners.front().Listen(UniqueSocketDescriptor(STDIN_FILENO));
	}

	/* daemonize II */

#ifdef __linux
	/* revert the "dumpable" flag to "true" after it was cleared by
	   setreuid() because we want core dumps to be able to analyze
	   crashes */
	prctl(PR_SET_DUMPABLE, 1, 0, 0, 0);
#endif

	try {
		capabilities_post_setuid(cap_keep_list, std::size(cap_keep_list));
	} catch (...) {
		/* if we failed to preserve CAP_NET_BIND_SERVICE, drop
		   all capabilities */
		static constexpr cap_value_t dummy{};
		capabilities_post_setuid(&dummy, 0);
	}

#ifdef HAVE_LIBSYSTEMD
	/* tell systemd we're ready */
	sd_notify(0, "READY=1");
#endif

	/* main loop */

	instance.event_loop.Dispatch();

	/* cleanup */

	thread_pool_deinit();
} catch (...) {
	PrintException(std::current_exception());
	return EXIT_FAILURE;
}
