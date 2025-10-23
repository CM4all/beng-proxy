// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "istream.hxx"
#include "memory/MultiFifoBuffer.hxx"

/**
 * Callbacks for #MultiFifoBufferIstream.
 */
class MultiFifoBufferIstreamHandler {
public:
	/**
	 * Called whenever some data has been consumed from the
	 * buffer.
	 */
	virtual void OnFifoBufferIstreamConsumed(size_t nbytes) noexcept = 0;

	/**
	 * Called while the #FifoBufferIstream is being closed.
	 */
	virtual void OnFifoBufferIstreamClosed() noexcept = 0;
};

/**
 * Similar to #FifoBufferIstream, but allocate multiple FIFO buffers
 * if necessary.
 */
class MultiFifoBufferIstream final : public Istream {
	MultiFifoBufferIstreamHandler &handler;

	MultiFifoBuffer buffer;

	bool eof = false;

public:
	MultiFifoBufferIstream(struct pool &p,
			       MultiFifoBufferIstreamHandler &_handler) noexcept
		:Istream(p),
		 handler(_handler) {}

	size_t GetAvailable() const noexcept {
		return buffer.GetAvailable();
	}

	/**
	 * Copy data into the FIFO buffer.  This will not invoke the
	 * #IstreamHandler and thus will never destroy the object.  To
	 * actually invoke the #IstreamHandler, call SubmitBuffer().
	 */
	void Push(std::span<const std::byte> src) noexcept {
		buffer.Push(src);
	}

	/**
	 * Indicate that this #Istream will end after all remaining
	 * data in the buffer has been consumed.  This will suppress
	 * any further #FifoBufferIstreamHandler calls.  This method
	 * may invoke the #IstreamHandler and destroy this object.
	 */
	void SetEof() noexcept;

	/**
	 * Pass the given error to the #IstreamHandler and destroy
	 * this object.
	 */
	using Istream::DestroyError;

	/**
	 * Submit data from the buffer to the #IstreamHandler.  After
	 * returning, this object may have been destroyed by the
	 * #IstreamHandler.
	 */
	void SubmitBuffer() noexcept;

	/* virtual methods from class Istream */

	IstreamLength _GetLength() noexcept override {
		return {
			.length = buffer.GetAvailable(),
			.exhaustive = eof,
		};
	}

	void _Read() noexcept override;
	void _FillBucketList(IstreamBucketList &list) noexcept override;
	ConsumeBucketResult _ConsumeBucketList(size_t nbytes) noexcept override;
	void _Close() noexcept override;
};
