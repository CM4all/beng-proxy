// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "config.h"

#include <memory>

class FailureManager;
class BalancerMap;
class FilteredSocketStock;
class FilteredSocketBalancer;
class SslClientFactory;
class LbMonitorManager;
namespace Avahi { class Client; class ErrorHandler; }

struct LbContext {
	FailureManager &failure_manager;
	BalancerMap &tcp_balancer;
	FilteredSocketStock &fs_stock;
	FilteredSocketBalancer &fs_balancer;
	SslClientFactory &ssl_client_factory;
	LbMonitorManager &monitors;
#ifdef HAVE_AVAHI
	std::unique_ptr<Avahi::Client> &avahi_client;
	Avahi::ErrorHandler &avahi_error_handler;

	[[gnu::const]]
	Avahi::Client &GetAvahiClient() const noexcept;
#endif
};
