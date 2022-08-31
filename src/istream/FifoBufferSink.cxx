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

#include "FifoBufferSink.hxx"
#include "Bucket.hxx"
#include "memory/fb_pool.hxx"
#include "io/Buffered.hxx"
#include "io/FileDescriptor.hxx"

#include <algorithm>
#include <exception>

#include <string.h>
#include <unistd.h>

bool
FifoBufferSink::OnIstreamReady() noexcept
{
	IstreamBucketList list;

	try {
		input.FillBucketList(list);
	} catch (...) {
		input.Clear();
		handler.OnFifoBufferSinkError(std::current_exception());
		return false;
	}

	std::size_t nbytes = 0;
	bool more = list.HasMore();

	for (const auto &bucket : list) {
		if (!bucket.IsBuffer()) {
			more = true;
			break;
		}

		buffer.AllocateIfNull(fb_pool_get());
		auto r = bucket.GetBuffer();
		std::size_t n_copy = buffer.MoveFrom(r);
		nbytes += n_copy;

		if (n_copy < r.size()) {
			more = true;
			break;
		}
	}

	if (nbytes > 0)
		input.ConsumeBucketList(nbytes);

	if (!more) {
		CloseInput();
		handler.OnFifoBufferSinkEof();
		return false;
	}

	return true;
}

std::size_t
FifoBufferSink::OnData(const void *data, std::size_t length) noexcept
{
	buffer.AllocateIfNull(fb_pool_get());
	const std::size_t nbytes = buffer.MoveFrom(std::span{(const std::byte *)data, length});

	if (!handler.OnFifoBufferSinkData())
		return 0;

	return nbytes;
}

IstreamDirectResult
FifoBufferSink::OnDirect(FdType, FileDescriptor fd, off_t offset,
			 std::size_t max_length) noexcept
{
	buffer.AllocateIfNull(fb_pool_get());

	const auto nbytes = HasOffset(offset)
		? ReadToBufferAt(fd, offset, buffer, max_length)
		: ReadToBuffer(fd, buffer, max_length);
	if (nbytes == -2)
		return IstreamDirectResult::BLOCKING;

	if (nbytes <= 0) {
		buffer.FreeIfEmpty();
		return nbytes < 0
			? IstreamDirectResult::ERRNO
			: IstreamDirectResult::END;
	}

	input.ConsumeDirect(nbytes);

	if (!handler.OnFifoBufferSinkData())
		return IstreamDirectResult::CLOSED;

	return IstreamDirectResult::OK;
}

void
FifoBufferSink::OnEof() noexcept
{
	input.Clear();
	handler.OnFifoBufferSinkEof();
}

void
FifoBufferSink::OnError(std::exception_ptr ep) noexcept
{
	input.Clear();
	handler.OnFifoBufferSinkError(std::move(ep));
}
