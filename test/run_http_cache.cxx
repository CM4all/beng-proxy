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

#include "PInstance.hxx"
#include "pool/pool.hxx"
#include "pool/tpool.hxx"
#include "pool/Ptr.hxx"
#include "cache.hxx"
#include "http_cache_heap.hxx"
#include "http_cache_info.hxx"
#include "strmap.hxx"
#include "rubber.hxx"
#include "AllocatorStats.hxx"

#include "util/Compiler.h"

#include <stdlib.h>

static void
put_random(HttpCacheHeap *cache)
{
	const TempPoolLease tpool;

	char uri[8];
	uri[0] = '0' + random() % 10;
	uri[1] = '0' + random() % 10;
	uri[2] = '0' + random() % 10;
	uri[3] = '0' + random() % 10;
	uri[4] = '0' + random() % 10;
	uri[5] = 0;

	HttpCacheResponseInfo info;
	info.expires = std::chrono::system_clock::from_time_t(1350000000);
	info.vary = "x-foo";

	size_t length = random() % (random() % (random() % (64 * 1024) + 1) + 1);
	unsigned rubber_id = 0;
	if (length > 0) {
		rubber_id = cache->GetRubber().Add(length);
		if (rubber_id == 0) {
			fprintf(stderr, "rubber_add(%zu) failed\n", length);
			return;
		}
	}

	auto *request_headers = strmap_new(tpool);

	if (random() % 3 == 0) {
		static const char *const values[] = {
			"a", "b", "c", "d", "e", "f", "g", "h",
		};
		request_headers->Add(*tpool, "x-foo", values[random() % 8]);
	}

	auto *response_headers = strmap_new(tpool);
	response_headers->Add(*tpool, "content-type", "text/plain");
	response_headers->Add(*tpool, "x-foo", "bar");
	response_headers->Add(*tpool, "x-bar", "foo");

	cache->Put(uri, info, *request_headers,
		   HTTP_STATUS_OK, *response_headers,
		   RubberAllocation(cache->GetRubber(), rubber_id), length);
}

/*
 * main
 *
 */

int
main(gcc_unused int argc, gcc_unused char **argv)
{
	static const size_t max_size = 256 * 1024 * 1024;

	PInstance instance;

	auto pool2 = pool_new_dummy(instance.root_pool, "cache");

	HttpCacheHeap cache(*pool2, instance.event_loop, max_size);

	for (unsigned i = 0; i < 32 * 1024; ++i)
		put_random(&cache);

	const auto stats = cache.GetStats();
	printf("netto=%zu brutto=%zu ratio=%f\n",
	       stats.netto_size, stats.brutto_size,
	       (double)stats.netto_size / stats.brutto_size);

	return EXIT_SUCCESS;
}
