// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "Protocol.hxx"
#include "SimpleHttpResponse.hxx"
#include "cluster/AddressList.hxx"
#include "cluster/StickyMode.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "config.h"

#ifdef HAVE_AVAHI
#include "ZeroconfDiscoveryConfig.hxx"
#endif

#include <memory>
#include <string>
#include <vector>
#include <forward_list>

struct LbMonitorConfig;

struct LbNodeConfig {
	std::string name;

	AllocatedSocketAddress address;

	/**
	 * The Tomcat "jvmRoute" setting of this node.  It is used for
	 * #StickyMode::JVM_ROUTE.
	 */
	std::string jvm_route;

	explicit LbNodeConfig(const char *_name) noexcept
		:name(_name) {}

	LbNodeConfig(const char *_name,
		     AllocatedSocketAddress &&_address) noexcept
		:name(_name), address(std::move(_address)) {}

	LbNodeConfig(LbNodeConfig &&src) noexcept
		:name(std::move(src.name)), address(std::move(src.address)),
		 jvm_route(std::move(src.jvm_route)) {}

	/**
	 * @return true if the address requires a port, but none was
	 * specified
	 */
	bool IsPortMissing() const noexcept {
		return address.HasPort() && address.GetPort() == 0;
	}
};

struct LbMemberConfig {
	const struct LbNodeConfig *node = nullptr;

	unsigned port = 0;

	/**
	 * @return true if the address requires a port, but none was
	 * specified
	 */
	bool IsPortMissing() const noexcept {
		return port == 0 && node->IsPortMissing();
	}
};

struct LbClusterConfig {
	std::string name;

	std::string http_host;

	/**
	 * The protocol that is spoken on this cluster.
	 */
	LbProtocol protocol = LbProtocol::HTTP;

	bool ssl = false;

	/**
	 * Send "Strict-Transport-Security" header?
	 */
	bool hsts = false;

	bool fair_scheduling = false;

	bool tarpit = false;

	/**
	 * Use the client's source IP for the connection to the backend?
	 * This is implemented using IP_TRANSPARENT and requires the
	 * "tproxy" Linux kernel module.
	 */
	bool transparent_source = false;

	bool mangle_via = false;

#ifdef HAVE_AVAHI
	enum class StickyMethod : uint_least8_t {
		CONSISTENT_HASHING,

		RENDEZVOUS_HASHING,

		/**
		 * Enable the #StickyCache for Zeroconf?
		 */
		CACHE,
	} sticky_method = StickyMethod::CONSISTENT_HASHING;
#endif

	LbSimpleHttpResponse fallback;

	StickyMode sticky_mode = StickyMode::NONE;

	/**
	 * If not empty and the URI begins with this prefix followed
	 * by 32 lower-case hex digits, then #sticky_mode will be
	 * ignored and instead a UUID will be generated from those 32
	 * hex digits.
	 */
	std::string sticky_hex_uuid_uri_prefix;

	std::string session_cookie = "beng_proxy_session";

	const LbMonitorConfig *monitor = nullptr;

	std::vector<LbMemberConfig> members;

#ifdef HAVE_AVAHI
	ZeroconfDiscoveryConfig zeroconf;
#endif

	std::unique_ptr<SocketAddress[]> address_list_allocation;
	std::forward_list<AllocatedSocketAddress> address_allocations;

	/**
	 * A list of node addresses.
	 */
	AddressList address_list;

	explicit LbClusterConfig(const char *_name) noexcept
		:name(_name) {}

	LbClusterConfig(LbClusterConfig &&) = default;

	LbClusterConfig(const LbClusterConfig &) = delete;
	LbClusterConfig &operator=(const LbClusterConfig &) = delete;

	/**
	 * Copy addresses of all members into the #AddressList.  This
	 * needs to be called before using this instance.
	 *
	 * Throws on error.
	 */
	void FillAddressList();

	/**
	 * Returns the member index of the node with the specified
	 * jvm_route value, or -1 if not found.
	 */
	[[gnu::pure]]
	int FindJVMRoute(std::string_view jvm_route) const noexcept;

	/**
	 * Returns the default port number for this cluster based on
	 * the configuration or 0 if there is no sensible default.
	 */
	unsigned GetDefaultPort() const noexcept {
		switch (protocol) {
		case LbProtocol::HTTP:
			return ssl ? 443 : 80;

		case LbProtocol::TCP:
			break;
		}

		return 0;
	}

	bool HasZeroConf() const noexcept {
#ifdef HAVE_AVAHI
		return zeroconf.IsEnabled();
#else
		return false;
#endif
	}
};
