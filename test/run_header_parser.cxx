// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "pool/RootPool.hxx"
#include "http/HeaderParser.hxx"
#include "memory/GrowingBuffer.hxx"
#include "AllocatorPtr.hxx"
#include "strmap.hxx"

#include <unistd.h>
#include <stdio.h>

int
main(int argc gcc_unused, char **argv gcc_unused)
{
	std::byte buffer[16];
	ssize_t nbytes;

	RootPool pool;
	const AllocatorPtr alloc{pool};

	GrowingBuffer gb;

	/* read input from stdin */

	while ((nbytes = read(0, buffer, sizeof(buffer))) > 0)
		gb.Write(std::span{buffer}.first(nbytes));

	/* parse the headers */

	auto *headers = strmap_new(pool);
	header_parse_buffer(alloc, *headers, std::move(gb));

	/* dump headers */

	for (const auto &i : *headers)
		printf("%s: %s\n", i.key, i.value);
}
