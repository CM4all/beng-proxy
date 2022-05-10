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
		InvokeData(zero_buffer, sizeof(zero_buffer));
	}

	void _FillBucketList(IstreamBucketList &list) noexcept override {
		list.SetMore();

		while (!list.IsFull())
			list.Push({zero_buffer, sizeof(zero_buffer)});
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
