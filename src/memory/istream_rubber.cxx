// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "istream_rubber.hxx"
#include "Rubber.hxx"
#include "istream/istream.hxx"
#include "istream/Bucket.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/New.hxx"

#include <algorithm>
#include <cassert>

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

	IstreamLength _GetLength() noexcept override {
		return {
			.length = end - position,
			.exhaustive = true,
		};
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
		const std::byte *data = (const std::byte *)rubber.Read(id);
		const size_t remaining = end - position;

		if (remaining > 0)
			list.Push({data + position, remaining});
	}

	ConsumeBucketResult _ConsumeBucketList(size_t nbytes) noexcept override {
		const size_t remaining = end - position;
		size_t consumed = std::min(nbytes, remaining);
		position += consumed;
		return {Consumed(consumed), position == end};
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
