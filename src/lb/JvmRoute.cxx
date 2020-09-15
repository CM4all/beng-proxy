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

#include "JvmRoute.hxx"
#include "ClusterConfig.hxx"
#include "strmap.hxx"
#include "pool/tpool.hxx"
#include "http/CookieServer.hxx"

#include <string.h>
#include <stdlib.h>

sticky_hash_t
lb_jvm_route_get(const StringMap &request_headers,
		 const LbClusterConfig &cluster)
{
	const TempPoolLease tpool;

	const char *cookie = request_headers.Get("cookie");
	if (cookie == NULL)
		return 0;

	const auto jar = cookie_map_parse(*tpool, cookie);

	const char *p = jar.Get("JSESSIONID");
	if (p == NULL)
		return 0;

	p = strchr(p, '.');
	if (p == NULL || p[1] == 0)
		return 0;

	const char *jvm_route = p + 1;
	int i = cluster.FindJVMRoute(jvm_route);
	if (i < 0)
		return 0;

	/* add num_members to make sure that the modulo still maps to the
	   node index, but the first node is not referred to as zero
	   (special value for "no session") */
	return i + cluster.members.size();
}
