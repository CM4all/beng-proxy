// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "ForwardIstream.hxx"

/**
 * An #Istream proxy which which provides a known length.  This can be
 * used by a HTTP client to propagate the Content-Length response
 * header, for example.
 */
class LengthIstream final : public ForwardIstream {
	off_t remaining;

public:
	template<typename P, typename I>
	LengthIstream(P &&_pool, I &&_input, off_t _length)
		:ForwardIstream(std::forward<P>(_pool),
				std::forward<I>(_input)),
		 remaining(_length) {}

	/* virtual methods from class Istream */

	IstreamLength _GetLength() noexcept override;
	off_t _Skip(off_t length) noexcept override;
	void _FillBucketList(IstreamBucketList &list) override;
	ConsumeBucketResult _ConsumeBucketList(std::size_t nbytes) noexcept override;
	void _ConsumeDirect(std::size_t nbytes) noexcept override;

protected:
	/* virtual methods from class IstreamHandler */

	std::size_t OnData(std::span<const std::byte> src) noexcept override;
	void OnEof() noexcept override;
};
