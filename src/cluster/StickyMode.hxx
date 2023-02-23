// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

/**
 * The "sticky" mode specifies which node of a cluster is chosen to
 * handle a request.
 */
enum class StickyMode {
	/**
	 * No specific mode, beng-lb may choose nodes at random.
	 */
	NONE,

	/**
	 * The first non-failing node is used.
	 */
	FAILOVER,

	/**
	 * Select the node with a hash of the client's IP address.
	 */
	SOURCE_IP,

	/**
	 * Select the node with a hash of the "Host" request header.
	 */
	HOST,

	/**
	 * Select the node with a hash of the "X-CM4all-Host" request header.
	 */
	XHOST,

	/**
	 * A modulo of the lower 32 bit of the beng-proxy session id is
	 * used to determine which worker shall be used.  Requires
	 * cooperation from beng-proxy on the nodes.
	 */
	SESSION_MODULO,

	/**
	 * A cookie is sent to the client, which is later used to direct
	 * its requests to the same cluster node.
	 */
	COOKIE,

	/**
	 * Tomcat with jvmRoute in cookie.
	 */
	JVM_ROUTE,
};
