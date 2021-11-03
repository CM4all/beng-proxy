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

#include "SinkGrowingBuffer.hxx"
#include "istream/Bucket.hxx"

#include <algorithm>

#include <string.h>
#include <unistd.h>

bool
GrowingBufferSink::OnIstreamReady() noexcept
{
	IstreamBucketList list;

	try {
		input.FillBucketList(list);
	} catch (...) {
		input.Clear();
		handler.OnGrowingBufferSinkError(std::current_exception());
		return false;
	}

	std::size_t nbytes = 0;
	bool more = list.HasMore();

	for (const auto &bucket : list) {
		if (!bucket.IsBuffer()) {
			more = true;
			break;
		}

		auto r = bucket.GetBuffer();
		buffer.Write(r.data, r.size);
		nbytes += r.size;
	}

	if (nbytes > 0)
		input.ConsumeBucketList(nbytes);

	if (!more) {
		CloseInput();
		handler.OnGrowingBufferSinkEof(std::move(buffer));
		return false;
	}

	return true;
}

std::size_t
GrowingBufferSink::OnData(const void *data, std::size_t length) noexcept
{
	buffer.Write(data, length);
	return length;
}

ssize_t
GrowingBufferSink::OnDirect(FdType, int fd, std::size_t max_length) noexcept
{
	auto w = buffer.BeginWrite();
	if (max_length < w.size)
		w.size = max_length;

	ssize_t nbytes = read(fd, w.data, w.size);
	if (nbytes > 0)
		buffer.CommitWrite(nbytes);

	return nbytes;
}

void
GrowingBufferSink::OnEof() noexcept
{
	input.Clear();
	handler.OnGrowingBufferSinkEof(std::move(buffer));
}

void
GrowingBufferSink::OnError(std::exception_ptr error) noexcept
{
	input.Clear();
	handler.OnGrowingBufferSinkError(std::move(error));
}
