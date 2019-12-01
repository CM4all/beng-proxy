/*
 * Copyright 2007-2019 Content Management AG
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
#include "fb_pool.hxx"

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

	size_t nbytes = 0;
	bool more = list.HasMore();

	for (const auto &bucket : list) {
		if (bucket.GetType() != IstreamBucket::Type::BUFFER) {
			more = true;
			break;
		}

		buffer.AllocateIfNull(fb_pool_get());
		auto w = buffer.Write();
		auto r = bucket.GetBuffer();
		size_t n_copy = std::min(w.size, r.size);
		memcpy(w.data, r.data, n_copy);
		nbytes += n_copy;

		if (n_copy < r.size) {
			more = true;
			break;
		}
	}

	if (nbytes > 0)
		input.ConsumeBucketList(nbytes);

	if (!more) {
		input.ClearAndClose();
		handler.OnFifoBufferSinkEof();
		return false;
	}

	return true;
}

size_t
FifoBufferSink::OnData(const void *data, size_t length) noexcept
{
	buffer.AllocateIfNull(fb_pool_get());

	auto w = buffer.Write();
	size_t nbytes = std::min(w.size, length);
	memcpy(w.data, data, nbytes);
	buffer.Append(nbytes);

	if (!handler.OnFifoBufferSinkData())
		return 0;

	return nbytes;
}

ssize_t
FifoBufferSink::OnDirect(FdType, int fd, size_t max_length) noexcept
{
	buffer.AllocateIfNull(fb_pool_get());

	auto w = buffer.Write();
	if (w.empty())
		return ISTREAM_RESULT_BLOCKING;

	if (max_length < w.size)
		w.size = max_length;

	ssize_t nbytes = read(fd, w.data, w.size);
	if (nbytes < 0)
		return ISTREAM_RESULT_ERRNO;

	if (nbytes == 0) {
		buffer.FreeIfEmpty();
		return ISTREAM_RESULT_EOF;
	}

	buffer.Append(nbytes);

	if (!handler.OnFifoBufferSinkData())
		return ISTREAM_RESULT_CLOSED;

	return nbytes;
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
