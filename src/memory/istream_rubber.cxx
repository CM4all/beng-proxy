// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "istream_rubber.hxx"
#include "Rubber.hxx"
#include "istream/istream.hxx"
#include "istream/Bucket.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/New.hxx"
#include "util/ConstBuffer.hxx"

#include <algorithm>

#include <assert.h>
#include <stdint.h>

class RubberIstream final : public Istream {
	Rubber &rubber;
	const unsigned id;
	const bool auto_remove;

	size_t position;
	const size_t end;

public:
	RubberIstream(struct pool &p, Rubber &_rubber, unsigned _id,
		      size_t start, size_t _end,
		      bool _auto_remove) noexcept
		:Istream(p), rubber(_rubber), id(_id), auto_remove(_auto_remove),
		 position(start), end(_end) {}

	~RubberIstream() noexcept override {
		if (auto_remove)
			rubber.Remove(id);
	}

	/* virtual methods from class Istream */

	off_t _GetAvailable(bool) noexcept override {
		return end - position;
	}

	off_t _Skip(off_t nbytes) noexcept override {
		assert(position <= end);

		const size_t remaining = end - position;
		if (nbytes > off_t(remaining))
			nbytes = remaining;

		position += nbytes;
		Consumed(nbytes);
		return nbytes;
	}

	void _Read() noexcept override {
		assert(position <= end);

		const std::byte *data = (const std::byte *)rubber.Read(id);
		const size_t remaining = end - position;

		if (remaining > 0) {
			size_t nbytes = InvokeData({data + position, remaining});
			if (nbytes == 0)
				return;

			position += nbytes;
		}

		if (position == end)
			DestroyEof();
	}

	void _FillBucketList(IstreamBucketList &list) override {
		const uint8_t *data = (const uint8_t *)rubber.Read(id);
		const size_t remaining = end - position;

		if (remaining > 0)
			list.Push(ConstBuffer<void>(data + position, remaining));
	}

	size_t _ConsumeBucketList(size_t nbytes) noexcept override {
		const size_t remaining = end - position;
		size_t consumed = std::min(nbytes, remaining);
		position += consumed;
		return Consumed(consumed);
	}
};

UnusedIstreamPtr
istream_rubber_new(struct pool &pool, Rubber &rubber,
		   unsigned id, size_t start, size_t end,
		   bool auto_remove) noexcept
{
	assert(id > 0);
	assert(start <= end);

	return NewIstreamPtr<RubberIstream>(pool, rubber, id,
					    start, end, auto_remove);
}
