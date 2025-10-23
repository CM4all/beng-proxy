// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "istream.hxx"
#include "memory/SliceFifoBuffer.hxx"

/**
 * Callbacks for #FifoBufferIstream.
 */
class FifoBufferIstreamHandler {
public:
	/**
	 * Called whenever some data has been consumed from the
	 * buffer.
	 */
	virtual void OnFifoBufferIstreamConsumed(size_t nbytes) noexcept = 0;

	/**
	 * Called after the buffer has become empty.
	 */
	virtual void OnFifoBufferIstreamDrained() noexcept = 0;

	/**
	 * Called while the #FifoBufferIstream is being closed.
	 */
	virtual void OnFifoBufferIstreamClosed() noexcept = 0;
};

/**
 * An #Istream implementation which reads data from a FIFO buffer
 * which may be filled by somebody using the Push() method.
 */
class FifoBufferIstream final : public Istream {
	FifoBufferIstreamHandler &handler;

	SliceFifoBuffer buffer;

	bool eof = false;

public:
	FifoBufferIstream(struct pool &p,
			  FifoBufferIstreamHandler &_handler) noexcept
		:Istream(p),
		 handler(_handler) {}

	auto &GetBuffer() noexcept {
		return buffer;
	}

	/**
	 * Copy data into the FIFO buffer.  This will not invoke the
	 * #IstreamHandler and thus will never destroy the object.  To
	 * actually invoke the #IstreamHandler, call SubmitBuffer().
	 *
	 * @return the number of bytes copied into the buffer
	 */
	size_t Push(std::span<const std::byte> src) noexcept;

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

	void _Close() noexcept override {
		if (!eof)
			handler.OnFifoBufferIstreamClosed();
		Istream::_Close();
	}
};
