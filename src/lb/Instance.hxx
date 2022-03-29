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

#pragma once

#include "PInstance.hxx"
#include "GotoMap.hxx"
#include "MonitorManager.hxx"
#include "stats/HttpStats.hxx"
#include "lib/avahi/ErrorHandler.hxx"
#include "event/FarTimerEvent.hxx"
#include "event/SignalEvent.hxx"
#include "event/ShutdownListener.hxx"
#include "net/FailureManager.hxx"
#include "io/Logger.hxx"
#include "lb_features.h"

#include <boost/intrusive/list.hpp>

#include <forward_list>
#include <memory>
#include <map>

class AccessLogGlue;
class PipeStock;
class BalancerMap;
class FilteredSocketStock;
class FilteredSocketBalancer;
struct LbConfig;
struct LbCertDatabaseConfig;
struct LbHttpConnection;
class LbTcpConnection;
class LbControl;
class LbListener;
class CertCache;
namespace BengProxy { struct ControlStats; }
namespace Avahi { class Client; class Publisher; struct Service; }

struct LbInstance final : PInstance, Avahi::ErrorHandler {
	const LbConfig &config;

	const Logger logger;

	ShutdownListener shutdown_listener;
	SignalEvent sighup_event;

	FarTimerEvent compress_event;

	HttpStats http_stats;

	std::forward_list<LbControl> controls;

	/* stock */
	FailureManager failure_manager;
	std::unique_ptr<BalancerMap> balancer;

	std::unique_ptr<FilteredSocketStock> fs_stock;
	std::unique_ptr<FilteredSocketBalancer> fs_balancer;

	std::unique_ptr<PipeStock> pipe_stock;

	LbMonitorManager monitors;

#ifdef HAVE_AVAHI
	std::unique_ptr<Avahi::Client> avahi_client;
	std::unique_ptr<Avahi::Publisher> avahi_publisher;
#endif

	LbGotoMap goto_map;

	std::forward_list<LbListener> listeners;

#ifdef ENABLE_CERTDB
	std::map<std::string, CertCache> cert_dbs;
#endif

	boost::intrusive::list<LbHttpConnection,
			       boost::intrusive::base_hook<boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>>>,
			       boost::intrusive::constant_time_size<true>> http_connections;

	boost::intrusive::list<LbTcpConnection,
			       boost::intrusive::base_hook<boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>>>,
			       boost::intrusive::constant_time_size<true>> tcp_connections;

	std::unique_ptr<AccessLogGlue> access_log;

	explicit LbInstance(const LbConfig &_config);
	~LbInstance() noexcept;

	auto &GetEventLoop() const noexcept {
		return shutdown_listener.GetEventLoop();
	}

	/**
	 * Transition the current process from "master" to "worker".  Call
	 * this after forking in the new worker process.
	 */
	void InitWorker();

	void InitAllListeners();
	void DeinitAllListeners() noexcept;

	void InitAllControls();
	void EnableAllControls() noexcept;
	void DeinitAllControls() noexcept;

	[[gnu::pure]]
	BengProxy::ControlStats GetStats() const noexcept;

	/**
	 * Compress memory allocators, try to return unused memory areas
	 * to the kernel.
	 */
	void Compress() noexcept;

#ifdef ENABLE_CERTDB
	CertCache &GetCertCache(const LbCertDatabaseConfig &cert_db_config);
	void ConnectCertCaches();
	void DisconnectCertCaches() noexcept;
#endif

	void FlushTranslationCaches() noexcept {
		goto_map.FlushCaches();
	}

	void InvalidateTranslationCaches(const TranslationInvalidateRequest &request) noexcept {
		goto_map.InvalidateTranslationCaches(request);
	}

	unsigned FlushSSLSessionCache(long tm) noexcept;

	void ShutdownCallback() noexcept;

	void ReloadEventCallback(int signo) noexcept;

private:
	void OnCompressTimer() noexcept;

	/* virtual methods from class Avahi::ErrorHandler */
	bool OnAvahiError(std::exception_ptr e) noexcept override;
};

void
init_signals(LbInstance *instance);

void
deinit_signals(LbInstance *instance);
