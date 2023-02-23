// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "BlockIstream.hxx"
#include "istream.hxx"
#include "UnusedPtr.hxx"
#include "New.hxx"

class BlockIstream final : public Istream {
public:
	explicit BlockIstream(struct pool &p):Istream(p) {}

	/* virtual methods from class Istream */

	void _Read() noexcept override {
	}
};

UnusedIstreamPtr
istream_block_new(struct pool &pool) noexcept
{
	return NewIstreamPtr<BlockIstream>(pool);
}
