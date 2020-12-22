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

#include "CommandLine.hxx"
#include "Instance.hxx"
#include "Listener.hxx"
#include "Connection.hxx"
#include "Global.hxx"
#include "crash.hxx"
#include "pool/pool.hxx"
#include "fb_pool.hxx"
#include "session/Glue.hxx"
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
#include "ua_classification.hxx"
#include "ssl/Init.hxx"
#include "ssl/Client.hxx"
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
	if (should_exit)
		return;

#ifdef HAVE_URING
	if (uring)
		uring->SetVolatile();
#endif

	should_exit = true;
	DisableSignals();
	thread_pool_stop();

	spawn->Shutdown();

	listeners.clear();

	connections.clear_and_dispose(BpConnection::Disposer());

	pool_commit();

#ifdef HAVE_AVAHI
	avahi_client.Close();
#endif

	compress_timer.Cancel();

	child_process_registry.SetVolatile();

	thread_pool_join();

	background_manager.AbortAll();

	session_save_timer.Cancel();
	session_save_deinit();
	session_manager_deinit();

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

	FadeChildren();

	if (widget_registry != nullptr)
		widget_registry->FlushCache();

	if (translation_caches)
		translation_caches->Flush();

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

void
BpInstance::AddListener(const BpConfig::Listener &c)
{
	listeners.emplace_front(*this, c.tag.empty() ? nullptr : c.tag.c_str(),
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
		if (local_address.IsDefined())
			avahi_client.AddService(c.zeroconf_service.c_str(),
						interface, local_address,
						c.v6only);
	}
#endif
}

int main(int argc, char **argv)
try {
	if (!IsKernelVersionOrNewer({4, 11}))
		throw "Your Linux kernel is too old; this program requires at least 4.11";

	InitProcessName(argc, argv);

#ifndef NDEBUG
	if (geteuid() != 0)
		debug_mode = true;
#endif

#if defined(HAVE_LIBSYSTEMD) || defined(HAVE_AVAHI)
	const ODBus::ScopeInit dbus_init;
	dbus_connection_set_exit_on_disconnect(ODBus::Connection::GetSystem(),
					       false);
#endif

	const ScopeFbPoolInit fb_pool_init;

	BpInstance instance;

	/* configuration */

	ParseCommandLine(instance.cmdline, instance.config, argc, argv);

	if (instance.cmdline.config_file != nullptr)
		LoadConfigFile(instance.config, instance.cmdline.config_file);

	instance.config.Finish(instance.cmdline.user,
			       debug_mode ? 8080 : 80);

	/* initialize */

	if (!instance.cmdline.user.IsEmpty()) {
		const char *runtime_directory = getenv("RUNTIME_DIRECTORY");
		if (runtime_directory != nullptr)
			/* since systemd starts beng-proxy as root, we
			   need to chown the RuntimeDirectory to the
			   final beng-proxy user; this should be fixed
			   eventually by launching beng-proxy as its
			   own user */
			chown(runtime_directory,
			      instance.cmdline.user.uid,
			      instance.cmdline.user.gid);
	}

	SetupProcess();

	if (instance.cmdline.ua_classification_file != nullptr)
		instance.ua_classification = std::make_unique<UserAgentClassList>(ua_classification_init(instance.cmdline.ua_classification_file));

	const ScopeSslGlobalInit ssl_init;
	ssl_client_init(instance.config.ssl_client);

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

	if (instance.cmdline.debug_listener_tag == nullptr) {
		for (const auto &i : instance.config.listen)
			instance.AddListener(i);
	} else {
		const char *tag = instance.cmdline.debug_listener_tag;
		if (*tag == 0)
			tag = nullptr;

		instance.listeners.emplace_front(instance, tag,
						 false, nullptr);
		instance.listeners.front().Listen(UniqueSocketDescriptor(STDIN_FILENO));
	}

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
					  });
	instance.spawn->SetHandler(instance);
	instance.spawn_service = instance.spawn.get();

	const ScopeCrashGlobalInit crash_init;

	session_manager_init(instance.event_loop,
			     instance.config.session_idle_timeout,
			     instance.config.cluster_size,
			     instance.config.cluster_node);

	if (!instance.config.session_save_path.empty()) {
		session_save_init(instance.config.session_save_path.c_str());
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
							&instance.cmdline.logger_user));

	if (instance.config.child_error_log.type != AccessLogConfig::Type::INTERNAL)
		instance.child_error_log.reset(AccessLogGlue::Create(instance.config.child_error_log,
								     &instance.cmdline.logger_user));

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

	if (!instance.config.translation_sockets.empty()) {
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

		instance.translation_service = instance.uncached_translation_service.get();

		if (instance.config.translate_cache_size > 0)
			instance.translation_service = instance.cached_translation_service.get();

		/* the WidgetRegistry class has its own cache and doesn't need
		   the TranslationCache */
		instance.widget_registry =
			new WidgetRegistry(instance.root_pool,
					   *instance.uncached_translation_service);
	}

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
					 instance.delegate_stock
#ifdef HAVE_LIBNFS
					 , instance.nfs_cache
#endif
					 );

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

	global_translation_service = instance.translation_service;
	global_pipe_stock = instance.pipe_stock;

	/* daemonize II */

	if (!instance.cmdline.user.IsEmpty())
		capabilities_pre_setuid();

	instance.cmdline.user.Apply();

#ifdef __linux
	/* revert the "dumpable" flag to "true" after it was cleared by
	   setreuid() because we want core dumps to be able to analyze
	   crashes */
	prctl(PR_SET_DUMPABLE, 1, 0, 0, 0);
#endif

	if (!instance.cmdline.user.IsEmpty())
		capabilities_post_setuid(cap_keep_list, std::size(cap_keep_list));

#ifdef HAVE_LIBSYSTEMD
	/* tell systemd we're ready */
	sd_notify(0, "READY=1");
#endif

	/* main loop */

	instance.event_loop.Dispatch();

	/* cleanup */

	thread_pool_deinit();

	ssl_client_deinit();
} catch (...) {
	PrintException(std::current_exception());
	return EXIT_FAILURE;
}
