/*
 * Copyright 2007-2019 Content Management AG
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
#include "Worker.hxx"
#include "Global.hxx"
#include "crash.hxx"
#include "pool/pool.hxx"
#include "fb_pool.hxx"
#include "session/Glue.hxx"
#include "session/Save.hxx"
#include "tcp_stock.hxx"
#include "translation/Stock.hxx"
#include "translation/Cache.hxx"
#include "cluster/TcpBalancer.hxx"
#include "fs/Stock.hxx"
#include "fs/Balancer.hxx"
#include "nghttp2/Stock.hxx"
#include "stock/MapStock.hxx"
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
#include "system/SetupProcess.hxx"
#include "system/ProcessName.hxx"
#include "system/Error.hxx"
#include "capabilities.hxx"
#include "spawn/Local.hxx"
#include "spawn/Glue.hxx"
#include "spawn/Client.hxx"
#include "net/SocketAddress.hxx"
#include "net/StaticSocketAddress.hxx"
#include "net/FailureManager.hxx"
#include "io/Logger.hxx"
#include "io/SpliceSupport.hxx"
#include "util/PrintException.hxx"

#if defined(HAVE_LIBSYSTEMD) || defined(HAVE_AVAHI)
#include "odbus/Init.hxx"
#include "odbus/Connection.hxx"
#endif

#ifdef HAVE_LIBSYSTEMD
#include <systemd/sd-daemon.h>
#endif

#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <sys/signal.h>
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

	spawn_worker_event.Cancel();

	child_process_registry.SetVolatile();

	thread_pool_join();

	KillAllWorkers();

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

void
BpInstance::AddTcpListener(int port)
{
	listeners.emplace_front(*this, nullptr, false, nullptr);
	auto &listener = listeners.front();
	listener.ListenTCP(port);
	listener.SetTcpDeferAccept(10);
}

int main(int argc, char **argv)
try {
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

	if (instance.config.ports.empty() && instance.config.listen.empty())
		instance.config.ports.push_back(debug_mode ? 8080 : 80);

	/* initialize */

	SetupProcess();

	if (instance.cmdline.ua_classification_file != nullptr)
		instance.ua_classification = std::make_unique<UserAgentClassList>(ua_classification_init(instance.cmdline.ua_classification_file));

	const ScopeSslGlobalInit ssl_init;
	ssl_client_init(instance.config.ssl_client);

	direct_global_init();

	instance.EnableSignals();

	for (auto i : instance.config.ports)
		instance.AddTcpListener(i);

	for (const auto &i : instance.config.listen)
		instance.AddListener(i);

	global_control_handler_init(&instance);

	if (instance.config.num_workers == 1)
		/* in single-worker mode with watchdog master process, let
		   only the one worker handle control commands */
		global_control_handler_disable(instance);

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

						  /* don't share the
						     DBus connection
						     with the
						     spawner */
						  dbus_shutdown();
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

	if (instance.config.translation_socket != nullptr) {
		instance.translation_stock =
			new TranslationStock(instance.event_loop,
					     instance.config.translation_socket,
					     instance.config.translate_stock_limit);
		instance.translation_service = instance.translation_stock;

		if (instance.config.translate_cache_size > 0) {
			instance.translation_cache =
				new TranslationCache(instance.root_pool,
						     instance.event_loop,
						     *instance.translation_stock,
						     instance.config.translate_cache_size,
						     false);
			instance.translation_service = instance.translation_cache;
		}

		/* the WidgetRegistry class has its own cache and doesn't need
		   the TranslationCache */
		instance.widget_registry =
			new WidgetRegistry(instance.root_pool,
					   *instance.translation_stock);
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

	/* create worker processes */

	if (instance.config.num_workers > 0) {
		/* the master process shouldn't work */
		instance.DisableListeners();

		/* spawn the first worker really soon */
		instance.spawn_worker_event.Schedule(std::chrono::milliseconds(10));
	} else {
		instance.InitWorker();
	}

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
