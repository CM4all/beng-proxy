// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "ForwardIstream.hxx"
#include "Bucket.hxx"

/**
 * An #Istream filter that disables the _FillBucketList() method.
 */
class NoBucketIstream : public ForwardIstream {
public:
	template<typename I>
	NoBucketIstream(struct pool &_pool, I &&_input) noexcept
		:ForwardIstream(_pool, std::forward<I>(_input)) {}

protected:
	void _FillBucketList(IstreamBucketList &list) override {
		list.EnableFallback();
	}
};

