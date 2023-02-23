// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "DefaultFifoBuffer.hxx"

#include <list>
#include <span>

class IstreamBucketList;

class MultiFifoBuffer {
	std::list<DefaultFifoBuffer> buffers;

public:
	MultiFifoBuffer() noexcept;
	~MultiFifoBuffer() noexcept;

	MultiFifoBuffer(MultiFifoBuffer &&) noexcept = default;
	MultiFifoBuffer &operator=(MultiFifoBuffer &&) noexcept = default;

	[[gnu::pure]]
	bool empty() const noexcept {
		return buffers.empty();
	}

	void Push(std::span<const std::byte> src) noexcept;

	[[gnu::pure]]
	size_t GetAvailable() const noexcept;

	std::span<const std::byte> Read() const noexcept;
	void Consume(size_t nbytes) noexcept;

	void FillBucketList(IstreamBucketList &list) const noexcept;

	/**
	 * Like Consume(), but can run across several buffers and the
	 * parameter is allowed to be larger than GetAvailable().
	 */
	size_t Skip(size_t nbytes) noexcept;
};
