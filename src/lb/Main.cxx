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
#include "TcpConnection.hxx"
#include "HttpConnection.hxx"
#include "Config.hxx"
#include "lb_check.hxx"
#include "fs/Stock.hxx"
#include "fs/Balancer.hxx"
#include "pipe_stock.hxx"
#include "access_log/Glue.hxx"
#include "ssl/Init.hxx"
#include "pool/pool.hxx"
#include "thread/Pool.hxx"
#include "fb_pool.hxx"
#include "capabilities.hxx"
#include "system/Isolate.hxx"
#include "system/SetupProcess.hxx"
#include "io/SpliceSupport.hxx"
#include "util/PrintException.hxx"

#if defined(HAVE_LIBSYSTEMD) || defined(HAVE_AVAHI)
#include "odbus/Init.hxx"
#include "odbus/Connection.hxx"
#endif

#ifdef HAVE_LIBSYSTEMD
#include <systemd/sd-daemon.h>
#endif

#include "lb_features.h"
#ifdef ENABLE_CERTDB
#include <libpq-fe.h>
#endif

#include <stdlib.h>

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

#ifdef HAVE_AVAHI
	avahi_client.Close();
#endif

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
	unsigned n_ssl_sessions = FlushSSLSessionCache(LONG_MAX);
	logger(3, "flushed ", n_ssl_sessions, " SSL sessions");

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
	if (geteuid() == 0)
		throw "Refusing to run as root";

	const ScopeFbPoolInit fb_pool_init;

	/* configuration */

	LbCmdLine cmdline;
	ParseCommandLine(cmdline, argc, argv);

	LbConfig config;
	LoadConfigFile(config, cmdline.config_path);

	LbInstance instance(cmdline, config);

	if (cmdline.check) {
		const ScopeSslGlobalInit ssl_init;
		lb_check(instance.event_loop, config);
		return EXIT_SUCCESS;
	}

	/* initialize */

	SetupProcess();
	capabilities_init();

	const ScopeSslGlobalInit ssl_init;

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
	instance.InitAllListeners();

	/* launch the access logger */

	instance.access_log.reset(AccessLogGlue::Create(config.access_log,
							&cmdline.logger_user));

	/* daemonize II */

#ifdef __linux
	/* revert the "dumpable" flag to "true" after it was cleared by
	   setreuid(); this is necessary for two reasons: (1) we want core
	   dumps to be able to analyze crashes; and (2) Linux kernels
	   older than 4.10 (commit 68eb94f16227) don't allow writing to
	   /proc/self/setgroups etc. without it, which
	   isolate_from_filesystem() needs to do */
	prctl(PR_SET_DUMPABLE, 1, 0, 0, 0);
#endif

	/* can't change to new (empty) rootfs if we may need to reconnect
	   to PostgreSQL eventually */
	// TODO: bind-mount the PostgreSQL socket into the new rootfs
	if (!config.HasCertDatabase())
		isolate_from_filesystem(config.HasZeroConf());

	if (config.HasTransparentSource()) {
		static constexpr cap_value_t cap_keep_list[] = {
			/* keep the NET_RAW capability to be able to
			   to use the socket option IP_TRANSPARENT */
			CAP_NET_RAW,
		};

		capabilities_post_setuid(cap_keep_list, std::size(cap_keep_list));
	} else {
		static constexpr cap_value_t dummy{};
		capabilities_post_setuid(&dummy, 0);
	}

#ifdef __linux
	prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
#endif

	/* main loop */

	instance.InitWorker();

#ifdef HAVE_LIBSYSTEMD
	/* tell systemd we're ready */
	sd_notify(0, "READY=1");
#endif

	instance.event_loop.Dispatch();

	/* cleanup */

	instance.DeinitAllListeners();
	instance.DeinitAllControls();

	thread_pool_deinit();
} catch (...) {
	PrintException(std::current_exception());
	return EXIT_FAILURE;
}
