// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "istream.hxx"

#include <span>

class MemoryIstream : public Istream {
	std::span<const std::byte> data;

public:
	MemoryIstream(struct pool &p, std::span<const std::byte> _data) noexcept
		:Istream(p), data(_data) {}

	/* virtual methods from class Istream */

	IstreamLength _GetLength() noexcept override {
		return {
			.length = static_cast<off_t>(data.size()),
			.exhaustive = true,
		};
	}

	off_t _Skip(off_t length) noexcept override;
	void _Read() noexcept override;
	void _FillBucketList(IstreamBucketList &list) noexcept override;
	ConsumeBucketResult _ConsumeBucketList(size_t nbytes) noexcept override;
};
