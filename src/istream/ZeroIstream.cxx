// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "ZeroIstream.hxx"
#include "istream.hxx"
#include "New.hxx"
#include "Bucket.hxx"

#include <limits.h>
#include <stdint.h>

static constexpr std::byte zero_buffer[4096]{};

class ZeroIstream final : public Istream {
public:
	explicit ZeroIstream(struct pool &_pool):Istream(_pool) {}

	/* virtual methods from class Istream */

	off_t _GetAvailable(bool partial) noexcept override {
		return partial
			? INT_MAX
			: -1;
	}

	off_t _Skip(off_t length) noexcept override {
		Consumed(length);
		return length;
	}

	void _Read() noexcept override {
		InvokeData(std::span{zero_buffer});
	}

	void _FillBucketList(IstreamBucketList &list) noexcept override {
		list.SetMore();

		while (!list.IsFull())
			list.Push(std::span{zero_buffer});
	}

	size_t _ConsumeBucketList(size_t nbytes) noexcept override {
		return Consumed(nbytes);
	}
};

UnusedIstreamPtr
istream_zero_new(struct pool &pool) noexcept
{
	return NewIstreamPtr<ZeroIstream>(pool);
}
