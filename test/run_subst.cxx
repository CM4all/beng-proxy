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
#include "istream/SubstIstream.hxx"
#include "istream/OpenFileIstream.hxx"
#include "fb_pool.hxx"
#include "PInstance.hxx"
#include "pool/pool.hxx"
#include "util/PrintException.hxx"
#include "util/StringView.hxx"

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
