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

#include "StdioSink.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/YamlSubstIstream.hxx"
#include "istream/OpenFileIstream.hxx"
#include "fb_pool.hxx"
#include "PInstance.hxx"
#include "pool/pool.hxx"
#include "util/ConstBuffer.hxx"
#include "util/PrintException.hxx"
#include "util/StringView.hxx"

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
