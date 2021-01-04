/*
 * Copyright 2007-2021 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "istream.hxx"
#include "MultiFifoBuffer.hxx"

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
	void Push(ConstBuffer<void> src) noexcept;

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

	off_t _GetAvailable(bool partial) noexcept override {
		return partial || eof
			? (off_t)buffer.GetAvailable()
			: (off_t)-1;
	}

	off_t _Skip(off_t length) noexcept override;
	void _Read() noexcept override;
	void _FillBucketList(IstreamBucketList &list) noexcept override;
	size_t _ConsumeBucketList(size_t nbytes) noexcept override;
	void _Close() noexcept override;
};
