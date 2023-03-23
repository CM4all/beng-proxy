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

	int _AsFd() noexcept override {
		/* fd0 is always linked with /dev/null */
		int fd = dup(0);
		if (fd < 0)
			return -1;

		Destroy();
		return fd;
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
