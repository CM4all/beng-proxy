// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "StdioSink.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/SubstIstream.hxx"
#include "istream/OpenFileIstream.hxx"
#include "memory/fb_pool.hxx"
#include "PInstance.hxx"
#include "pool/pool.hxx"
#include "util/PrintException.hxx"

int
main(int argc, char **argv)
try {
	int i;

	const ScopeFbPoolInit fb_pool_init;
	PInstance instance;

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);

	SubstTree tree;

	for (i = 1; i <= argc - 2; i += 2) {
		tree.Add(pool, argv[i], argv[i + 1]);
	}

	if (i < argc) {
		fprintf(stderr, "usage: %s [A1 B1 A2 B2 ...]\n", argv[0]);
		return 1;
	}

	StdioSink sink(istream_subst_new(pool,
					 OpenFileIstream(instance.event_loop, pool,
							 "/dev/stdin"),
					 std::move(tree)));

	pool.reset();
	pool_commit();

	sink.LoopRead();
} catch (...) {
	PrintException(std::current_exception());
	return EXIT_FAILURE;
}
