// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "http/CookieClient.hxx"
#include "http/CookieJar.hxx"
#include "http/HeaderWriter.hxx"
#include "pool/RootPool.hxx"
#include "memory/fb_pool.hxx"
#include "strmap.hxx"
#include "memory/GrowingBuffer.hxx"
#include "AllocatorPtr.hxx"

#include <stdio.h>
#include <unistd.h>
#include <string.h>

int
main(int argc, char **argv) noexcept
{
	const ScopeFbPoolInit fb_pool_init;
	RootPool pool;
	const AllocatorPtr alloc(pool);

	CookieJar jar;

	for (int i = 1; i < argc; ++i)
		cookie_jar_set_cookie2(jar, argv[i], "foo.bar", nullptr);

	StringMap headers;
	cookie_jar_http_header(jar, "foo.bar", "/x", headers, alloc);

	GrowingBufferReader reader(headers_dup(headers));

	while (true) {
		const auto src = reader.Read();
		if (src.data() ==nullptr)
			break;

		ssize_t nbytes = write(1, src.data(), src.size());
		if (nbytes < 0) {
			perror("write() failed");
			return 1;
		}

		if (nbytes == 0)
			break;

		reader.Consume((size_t)nbytes);
	}
}
