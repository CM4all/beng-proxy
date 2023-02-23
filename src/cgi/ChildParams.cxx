// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "ChildParams.hxx"
#include "spawn/ChildOptions.hxx"
#include "pool/StringBuilder.hxx"
#include "AllocatorPtr.hxx"

const char *
CgiChildParams::GetStockKey(AllocatorPtr alloc) const noexcept
{
	PoolStringBuilder<256> b;
	b.push_back(executable_path);

	for (auto i : args) {
		b.push_back(" ");
		b.push_back(i);
	}

	for (auto i : options.env) {
		b.push_back("$");
		b.push_back(i);
	}

	char options_buffer[16384];
	b.emplace_back(options_buffer,
		       options.MakeId(options_buffer));

	return b(alloc);
}
