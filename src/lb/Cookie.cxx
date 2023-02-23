// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Cookie.hxx"
#include "pool/tpool.hxx"
#include "http/CookieServer.hxx"
#include "util/StringCompare.hxx"
#include "AllocatorPtr.hxx"
#include "strmap.hxx"

#include <assert.h>
#include <stdlib.h>

sticky_hash_t
lb_cookie_get(const StringMap &request_headers)
{
	const TempPoolLease tpool;

	const char *cookie = request_headers.Get("cookie");
	if (cookie == NULL)
		return 0;

	const auto jar = cookie_map_parse(*tpool, cookie);

	const char *p = jar.Get("beng_lb_node");
	if (p == nullptr)
		return 0;

	p = StringAfterPrefix(p, "0-");
	if (p == nullptr)
		return 0;

	char *endptr;
	unsigned long id = strtoul(p, &endptr, 16);
	if (endptr == p || *endptr != 0)
		return 0;

	return (sticky_hash_t)id;
}

sticky_hash_t
lb_cookie_generate(unsigned n)
{
	assert(n >= 2);

	return (random() % n) + 1;
}
