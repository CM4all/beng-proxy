/*
 * Copyright 2007-2020 CM4all GmbH
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
#include "PipeLease.hxx"
#include "SliceFifoBuffer.hxx"

class SliceBuffer;

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

	~PipeLeaseIstream() noexcept override {
		pipe.Release(remaining == 0);
	}

	/* virtual methods from class Istream */

	void _SetDirect(FdTypeMask mask) noexcept override {
		direct = (mask & FdTypeMask(FdType::FD_PIPE)) != 0;
	}

	off_t _GetAvailable(bool) noexcept override {
		return remaining;
	}

	off_t _Skip(off_t length) noexcept override;

	void _Read() noexcept override;

private:
	/**
	 * @return true if the buffer is now empty; false if data remains
	 * in the buffer or if the #Istream has been closed
	 */
	bool FeedBuffer() noexcept;
};
