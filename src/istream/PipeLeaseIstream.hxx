// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "istream.hxx"
#include "pipe/Lease.hxx"
#include "memory/SliceFifoBuffer.hxx"

/**
 * Read data from a #PipeLease.  The data must be in the pipe already,
 * and no more new data must be written to it.
 */
class PipeLeaseIstream final : public Istream {
	PipeLease pipe;

	/**
	 * Remaining data in the pipe.  Data which has been transferred
	 * into our buffer doesn't count.
	 */
	size_t remaining;

	SliceFifoBuffer buffer;

	bool direct = false;

public:
	PipeLeaseIstream(struct pool &p, PipeLease &&_pipe, size_t size) noexcept
		:Istream(p), pipe(std::move(_pipe)), remaining(size) {}

	~PipeLeaseIstream() noexcept override;

	/* virtual methods from class Istream */

	void _SetDirect(FdTypeMask mask) noexcept override {
		direct = (mask & FdTypeMask(FdType::FD_PIPE)) != 0;
	}

	IstreamLength _GetLength() noexcept override {
		return {
			.length = remaining,
			.exhaustive = true,
		};
	}

	void _Read() noexcept override;
	void _ConsumeDirect(std::size_t nbytes) noexcept override;

private:
	/**
	 * @return true if the buffer is now empty; false if data remains
	 * in the buffer or if the #Istream has been closed
	 */
	bool FeedBuffer() noexcept;
};
