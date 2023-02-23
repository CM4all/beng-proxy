// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "StdioSink.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/YamlSubstIstream.hxx"
#include "istream/OpenFileIstream.hxx"
#include "memory/fb_pool.hxx"
#include "PInstance.hxx"
#include "pool/pool.hxx"
#include "util/ConstBuffer.hxx"
#include "util/PrintException.hxx"

struct Usage {};

int
main(int argc, char **argv)
try {
	ConstBuffer<const char *> args(argv + 1, argc - 1);
	if (args.empty())
		throw Usage();

	const char *const prefix = args.shift();
	if (args.empty())
		throw Usage();

	const char *const yaml_file = args.shift();
	const char *const yaml_map_path = args.empty() ? nullptr : args.shift();

	if (!args.empty())
		throw Usage();

	const ScopeFbPoolInit fb_pool_init;
	PInstance instance;

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);

	StdioSink sink(NewYamlSubstIstream(pool,
					   OpenFileIstream(instance.event_loop, *pool,
							   "/dev/stdin"),
					   true,
					   prefix, yaml_file, yaml_map_path));

	pool.reset();
	pool_commit();

	sink.LoopRead();
} catch (Usage) {
	fprintf(stderr, "usage: %s PREFIX DATA.yaml [MAP_PATH]\n", argv[0]);
	return EXIT_FAILURE;
} catch (...) {
	PrintException(std::current_exception());
	return EXIT_FAILURE;
}
