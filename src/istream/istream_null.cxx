// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "istream_null.hxx"
#include "istream.hxx"
#include "New.hxx"

#include <unistd.h>

class NullIstream final : public Istream {
public:
	NullIstream(struct pool &p)
		:Istream(p) {}

	/* virtual methods from class Istream */

	off_t _GetAvailable(bool) noexcept override {
		return 0;
	}

	void _Read() noexcept override {
		DestroyEof();
	}

	void _FillBucketList(IstreamBucketList &) override {
	}

	ConsumeBucketResult _ConsumeBucketList(size_t) noexcept override {
		return {0, true};
	}

	void _Close() noexcept override {
		Destroy();
	}
};

UnusedIstreamPtr
istream_null_new(struct pool &pool)
{
	return NewIstreamPtr<NullIstream>(pool);
}
