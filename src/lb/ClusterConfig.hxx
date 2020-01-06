/*
 * Copyright 2007-2019 CM4all GmbH
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

#include "SimpleHttpResponse.hxx"
#include "address_list.hxx"
#include "StickyMode.hxx"
#include "net/AllocatedSocketAddress.hxx"

#include <string>
#include <vector>
#include <forward_list>

struct LbMonitorConfig;

enum class LbProtocol {
	HTTP,
	TCP,
};

struct LbNodeConfig {
	std::string name;

	AllocatedSocketAddress address;

	/**
	 * The Tomcat "jvmRoute" setting of this node.  It is used for
	 * #StickyMode::JVM_ROUTE.
	 */
	std::string jvm_route;

	explicit LbNodeConfig(const char *_name)
		:name(_name) {}

	LbNodeConfig(const char *_name, AllocatedSocketAddress &&_address)
		:name(_name), address(std::move(_address)) {}

	LbNodeConfig(LbNodeConfig &&src)
		:name(std::move(src.name)), address(std::move(src.address)),
		 jvm_route(std::move(src.jvm_route)) {}
};

struct LbMemberConfig {
	const struct LbNodeConfig *node = nullptr;

	unsigned port = 0;
};

struct LbClusterConfig {
	std::string name;

	/**
	 * The protocol that is spoken on this cluster.
	 */
	LbProtocol protocol = LbProtocol::HTTP;

	/**
	 * Use the client's source IP for the connection to the backend?
	 * This is implemented using IP_TRANSPARENT and requires the
	 * "tproxy" Linux kernel module.
	 */
	bool transparent_source = false;

	bool mangle_via = false;

#ifdef HAVE_AVAHI
	/**
	 * Enable the #StickyCache for Zeroconf?  By default, consistent
	 * hashing using #HashRing is used.
	 */
	bool sticky_cache = false;
#endif

	LbSimpleHttpResponse fallback;

	StickyMode sticky_mode = StickyMode::NONE;

	std::string session_cookie = "beng_proxy_session";

	const LbMonitorConfig *monitor = nullptr;

	std::vector<LbMemberConfig> members;

#ifdef HAVE_AVAHI
	std::string zeroconf_service, zeroconf_domain;
#endif

	std::forward_list<AllocatedSocketAddress> address_allocations;

	/**
	 * A list of node addresses.
	 */
	AddressList address_list;

	explicit LbClusterConfig(const char *_name)
		:name(_name) {}

	LbClusterConfig(LbClusterConfig &&) = default;

	LbClusterConfig(const LbClusterConfig &) = delete;
	LbClusterConfig &operator=(const LbClusterConfig &) = delete;

	/**
	 * Copy addresses of all members into the #AddressList.  This
	 * needs to be called before using this instance.
	 */
	void FillAddressList();

	/**
	 * Returns the member index of the node with the specified
	 * jvm_route value, or -1 if not found.
	 */
	gcc_pure
	int FindJVMRoute(const char *jvm_route) const;

	bool HasZeroConf() const {
#ifdef HAVE_AVAHI
		return !zeroconf_service.empty();
#else
		return false;
#endif
	}
};
