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
