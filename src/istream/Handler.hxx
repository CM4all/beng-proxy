/*
 * Copyright 2007-2022 CM4all GmbH
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

#include "Result.hxx"
#include "io/FdType.hxx"

#include <cstddef>
#include <exception>
#include <span>

#include <sys/types.h>

class FileDescriptor;

/** data sink for an istream */
class IstreamHandler {
public:
	/**
	 * @see OnDirect()
	 */
	static constexpr off_t NO_OFFSET = -1;

	/**
	 * Determine whether the given offset is an explicit offset,
	 * or whether #NO_OFFSET was given.
	 */
	static constexpr bool HasOffset(off_t offset) noexcept {
		return offset >= 0;
	}

	/**
	 * Convert an offset into a pointer argument for splice() and
	 * pread().
	 */
	static constexpr off_t *ToOffsetPointer(off_t &offset) noexcept {
		return HasOffset(offset) ? &offset : nullptr;
	}

	/**
	 * Data is available and the callee shall invoke
	 * Istream::FillBucketList() and Istream::ConsumeBucketList().
	 *
	 * This is the successor to OnData() and OnDirect().  Once
	 * everything has been migrated to #IstreamBucketList, these
	 * methods can be removed.
	 *
	 * @return true if the caller shall invoke OnData() or OnDirect(),
	 * false if data has already been handled or if the #Istream has
	 * been closed
	 */
	virtual bool OnIstreamReady() noexcept {
		return true;
	}

	/**
	 * Data is available as a buffer.
	 * This function must return 0 if it has closed the stream.
	 *
	 * @param data the buffer
	 * @param length the number of bytes available in the buffer, greater than 0
	 * @return the number of bytes consumed, 0 if writing would block
	 * (caller is responsible for registering an event) or if the
	 * stream has been closed
	 */
	virtual std::size_t OnData(std::span<const std::byte> src) noexcept = 0;

	/**
	 * Data is available in a file descriptor.
	 *
	 * After the method has read data from the specified file
	 * descriptor, it must call Istream::ConsumeDirect().
	 *
	 * @param type what kind of file descriptor?
	 * @param fd the file descriptor
	 * @param offset read from the file descriptor at the given
	 * offset; pass #NO_OFFSET to read from the current offset or if not
	 * applicable (e.g. pipes, sockets)
	 * @param max_length don't read more than this number of bytes
	 * @return the number of bytes consumed, or one of the
	 * #istream_result values
	 */
	virtual IstreamDirectResult OnDirect(FdType type, FileDescriptor fd,
					     off_t offset,
					     std::size_t max_length) noexcept;

	/**
	 * End of file encountered.
	 */
	virtual void OnEof() noexcept = 0;

	/**
	 * The istream has ended unexpectedly, e.g. an I/O error.
	 *
	 * The method Istream::Close() will not result in a call to
	 * this callback, since the caller is assumed to be the
	 * istream handler.
	 *
	 * @param error an exception describing the error condition
	 */
	virtual void OnError(std::exception_ptr error) noexcept = 0;
};
