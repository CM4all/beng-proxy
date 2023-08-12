// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "istream_unlock.hxx"
#include "istream/ForwardIstream.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/New.hxx"
#include "cache.hxx"

class UnlockIstream final : public ForwardIstream {
	CacheItem &item;

public:
	UnlockIstream(struct pool &p, UnusedIstreamPtr _input,
		      CacheItem &_item) noexcept
		:ForwardIstream(p, std::move(_input)),
		 item(_item) {
		item.Lock();
	}

	virtual ~UnlockIstream() noexcept override {
		item.Unlock();
	}
};

UnusedIstreamPtr
istream_unlock_new(struct pool &pool, UnusedIstreamPtr input, CacheItem &item)
{
	return NewIstreamPtr<UnlockIstream>(pool, std::move(input), item);
}
