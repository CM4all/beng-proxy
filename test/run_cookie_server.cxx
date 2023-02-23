// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "http/CookieServer.hxx"
#include "strmap.hxx"
#include "pool/RootPool.hxx"
#include "AllocatorPtr.hxx"

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

int
main(int argc, char **argv) noexcept
{
	RootPool pool;
	const AllocatorPtr alloc(pool);

	StringMap cookies;
	for (int i = 1; i < argc; ++i)
		cookies.Merge(cookie_map_parse(alloc, argv[i]));

	for (const auto &i : cookies)
		printf("%s=%s\n", i.key, i.value);
}
