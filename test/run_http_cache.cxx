// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "PInstance.hxx"
#include "pool/pool.hxx"
#include "pool/tpool.hxx"
#include "pool/Ptr.hxx"
#include "http/CommonHeaders.hxx"
#include "http/Status.hxx"
#include "http/cache/Heap.hxx"
#include "http/cache/Info.hxx"
#include "strmap.hxx"
#include "memory/Rubber.hxx"
#include "memory/AllocatorStats.hxx"
#include "AllocatorPtr.hxx"

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
	response_headers->Add(*tpool, content_type_header, "text/plain");
	response_headers->Add(*tpool, "x-foo", "bar");
	response_headers->Add(*tpool, "x-bar", "foo");

	cache->Put(StringWithHash{uri}, nullptr, info, *request_headers,
		   HttpStatus::OK, *response_headers,
		   RubberAllocation(cache->GetRubber(), rubber_id), length);
}

/*
 * main
 *
 */

int
main(int, char **) noexcept
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
