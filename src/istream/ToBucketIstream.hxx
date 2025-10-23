// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "FacadeIstream.hxx"
#include "event/DeferEvent.hxx"
#include "memory/SliceFifoBuffer.hxx"

/**
 * This class is an adapter for an existing #Istream implementation
 * which guarantees that FillBucketList() is available.  If the
 * underlying #Istream doesn't support it, it will copy incoming data
 * into its own buffer and make it available in a bucket.
 */
class ToBucketIstream final : public FacadeIstream {
	SliceFifoBuffer buffer;

	DeferEvent defer_read;

public:
	ToBucketIstream(struct pool &_pool, EventLoop &_event_loop,
			UnusedIstreamPtr &&_input) noexcept;

private:
	void DeferredRead() noexcept {
		input.Read();
	}

protected:
	/* virtual methods from class Istream */

	void _Read() noexcept override;
	void _FillBucketList(IstreamBucketList &list) override;
	ConsumeBucketResult _ConsumeBucketList(size_t nbytes) noexcept override;

	/* virtual methods from class IstreamHandler */

	IstreamReadyResult OnIstreamReady() noexcept override;
	size_t OnData(std::span<const std::byte> src) noexcept override;
	void OnEof() noexcept override;
	void OnError(std::exception_ptr &&ep) noexcept override;
};
