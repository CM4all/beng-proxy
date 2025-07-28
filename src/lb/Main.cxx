// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "CommandLine.hxx"
#include "Instance.hxx"
#include "TcpConnection.hxx"
#include "HttpConnection.hxx"
#include "Config.hxx"
#include "lb_check.hxx"
#include "fs/Stock.hxx"
#include "fs/Balancer.hxx"
#include "pipe/Stock.hxx"
#include "access_log/Glue.hxx"
#include "ssl/Init.hxx"
#include "pool/pool.hxx"
#include "thread/Pool.hxx"
#include "memory/fb_pool.hxx"
#include "net/InterfaceNameCache.hxx"
#include "system/Isolate.hxx"
#include "system/SetupProcess.hxx"
#include "io/SpliceSupport.hxx"
#include "util/PrintException.hxx"
#include "config.h"

#ifdef HAVE_LIBCAP
#include "system/Capabilities.hxx"
#endif // HAVE_LIBCAP

#if defined(HAVE_LIBSYSTEMD) || defined(HAVE_AVAHI)
#include "lib/dbus/Init.hxx"
#include "lib/dbus/Connection.hxx"
#endif

#ifdef HAVE_AVAHI
#include "lib/avahi/Client.hxx"
#include "lib/avahi/Publisher.hxx"
#endif

#ifdef HAVE_LIBSYSTEMD
#include <systemd/sd-daemon.h>
#endif

#include "lb_features.h"
#ifdef ENABLE_CERTDB
#include <libpq-fe.h>
#endif

#include <stdlib.h>
#include <sysexits.h> // for EX_*

#ifdef __linux
#include <sys/prctl.h>

#ifndef PR_SET_NO_NEW_PRIVS
#define PR_SET_NO_NEW_PRIVS 38
#endif
#endif

void
LbInstance::ShutdownCallback() noexcept
{
	deinit_signals(this);
	thread_pool_stop();

	compress_event.Cancel();

	DeinitAllControls();

	while (!tcp_connections.empty())
		tcp_connections.front().Destroy();

	while (!http_connections.empty())
		http_connections.front().CloseAndDestroy();

	goto_map.Clear();

#ifdef ENABLE_CERTDB
	DisconnectCertCaches();
#endif

	DeinitAllListeners();

#ifdef HAVE_AVAHI
	avahi_publisher.reset();
	avahi_client.reset();
#endif

	thread_pool_join();

	monitors.clear();

	pool_commit();

	fs_balancer.reset();
	fs_stock.reset();

	balancer.reset();

	pipe_stock.reset();

	pool_commit();
}

void
LbInstance::ReloadEventCallback(int) noexcept
{
	FlushInterfaceNameCache();

	goto_map.FlushCaches();

	Compress();
}

void
init_signals(LbInstance *instance)
{
	instance->shutdown_listener.Enable();
	instance->sighup_event.Enable();
}

void
deinit_signals(LbInstance *instance)
{
	instance->shutdown_listener.Disable();
	instance->sighup_event.Disable();
}

int
main(int argc, char **argv)
try {
	const ScopeFbPoolInit fb_pool_init;

	/* configuration */

	LbCmdLine cmdline;
	LbConfig config;

	ParseCommandLine(cmdline, config, argc, argv);

	if (geteuid() == 0)
		throw "Refusing to run as root";

	try {
		LoadConfigFile(config, cmdline.config_path);
	} catch (...) {
		PrintException(std::current_exception());
		return EX_CONFIG;
	}

	const ScopeSslGlobalInit ssl_init;

	LbInstance instance(config);

	if (cmdline.check) {
		lb_check(instance.event_loop, config);
		return EXIT_SUCCESS;
	}

	/* initialize */

	SetupProcess();

	/* force line buffering so Lua "print" statements are flushed
	   even if stdout is a pipe to systemd-journald */
	setvbuf(stdout, nullptr, _IOLBF, 0);
	setvbuf(stderr, nullptr, _IOLBF, 0);

#ifdef HAVE_LIBCAP
	capabilities_init();
#endif // HAVE_LIBCAP

#if defined(HAVE_LIBSYSTEMD) || defined(HAVE_AVAHI)
	const ODBus::ScopeInit dbus_init;
	dbus_connection_set_exit_on_disconnect(ODBus::Connection::GetSystem(),
					       false);
#endif

#ifdef ENABLE_CERTDB
	/* prevent libpq from initializing libssl & libcrypto again */
	PQinitOpenSSL(0, 0);
#endif

	direct_global_init();

	init_signals(&instance);

	instance.InitAllControls();
	instance.InitAllListeners(&cmdline.logger_user);

	/* daemonize II */

	/* can't change to new (empty) rootfs if we may need to reconnect
	   to PostgreSQL eventually */
	// TODO: bind-mount the PostgreSQL socket into the new rootfs
	if (!config.HasCertDatabase() && getenv("SSLKEYLOGFILE") == nullptr)
		isolate_from_filesystem(config.HasZeroConf(),
					config.HasPrometheusExporter());


#ifdef HAVE_LIBCAP
	if (config.HasTransparentSource()) {
		static constexpr cap_value_t cap_keep_list[] = {
			/* keep the NET_RAW capability to be able to
			   to use the socket option IP_TRANSPARENT */
			CAP_NET_RAW,
		};

		capabilities_post_setuid(cap_keep_list);
	} else {
		capabilities_post_setuid({});
	}
#endif // HAVE_LIBCAP

#ifdef __linux
	prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
#endif

	/* main loop */

	instance.InitWorker();

#ifdef HAVE_LIBSYSTEMD
	/* tell systemd we're ready */
	sd_notify(0, "READY=1");
#endif

	instance.event_loop.Run();

	/* cleanup */

	instance.DeinitAllListeners();
	instance.DeinitAllControls();

	thread_pool_deinit();
} catch (...) {
	PrintException(std::current_exception());
	return EXIT_FAILURE;
}
