// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "istream_gb.hxx"
#include "istream/istream.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/New.hxx"
#include "GrowingBuffer.hxx"
#include "util/ConstBuffer.hxx"

#include <utility>

class GrowingBufferIstream final : public Istream {
	GrowingBufferReader reader;

public:
	GrowingBufferIstream(struct pool &p, GrowingBuffer &&_gb)
		:Istream(p), reader(std::move(_gb)) {
		_gb.Release();
	}

	/* virtual methods from class Istream */

	off_t _GetAvailable(bool) noexcept override {
		return reader.Available();
	}

	off_t _Skip(off_t _nbytes) noexcept override {
		size_t nbytes = _nbytes > off_t(reader.Available())
			? reader.Available()
			: size_t(_nbytes);

		reader.Skip(nbytes);
		return Consumed(nbytes);
	}

	void _Read() noexcept override {
		/* this loop is required to cross the buffer borders */
		while (true) {
			auto src = reader.Read();
			if (src.empty()) {
				assert(reader.IsEOF());
				DestroyEof();
				return;
			}

			assert(!reader.IsEOF());

			size_t nbytes = InvokeData(src);
			if (nbytes == 0)
				/* growing_buffer has been closed */
				return;

			reader.Consume(nbytes);
			if (nbytes < src.size())
				return;
		}
	}

	void _FillBucketList(IstreamBucketList &list) override {
		reader.FillBucketList(list);
	}

	ConsumeBucketResult _ConsumeBucketList(size_t nbytes) noexcept override {
		size_t consumed = reader.ConsumeBucketList(nbytes);
		return {Consumed(consumed), reader.IsEOF()};
	}
};

UnusedIstreamPtr
istream_gb_new(struct pool &pool, GrowingBuffer &&gb) noexcept
{
	return UnusedIstreamPtr(NewIstream<GrowingBufferIstream>(pool, std::move(gb)));
}
