/*
 * Copyright 2007-2019 CM4all GmbH
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

#include "MultiFifoBuffer.hxx"
#include "SliceFifoBuffer.hxx"
#include "istream/Bucket.hxx"
#include "util/ConstBuffer.hxx"

#include <string.h>

MultiFifoBuffer::MultiFifoBuffer() noexcept = default;
MultiFifoBuffer::~MultiFifoBuffer() noexcept = default;

void
MultiFifoBuffer::Push(ConstBuffer<uint8_t> src) noexcept
{
	/* try to append to the last existing buffer (if there is
	   any) */
	if (!buffers.empty()) {
		auto &b = buffers.back();
		assert(b.IsDefined());

		auto w = b.Write();
		size_t nbytes = std::min(w.size, src.size);
		memcpy(w.data, src.data, nbytes);
		b.Append(nbytes);
		src.skip_front(nbytes);
	}

	/* create more buffers for remaining data */
	while (!src.empty()) {
		buffers.emplace_back();
		auto &b = buffers.back();
		b.Allocate();

		auto w = b.Write();
		size_t nbytes = std::min(w.size, src.size);
		memcpy(w.data, src.data, nbytes);
		b.Append(nbytes);
		src.skip_front(nbytes);
	}
}

size_t
MultiFifoBuffer::GetAvailable() const noexcept
{
	size_t result = 0;
	for (const auto &i : buffers)
		result += i.GetAvailable();
	return result;
}

ConstBuffer<uint8_t>
MultiFifoBuffer::Read() const noexcept
{
	if (buffers.empty())
		return nullptr;

	return buffers.front().Read();
}

void
MultiFifoBuffer::Consume(size_t nbytes) noexcept
{
	if (nbytes == 0)
		return;

	assert(!buffers.empty());

	auto &b = buffers.front();
	assert(b.IsDefined());
	assert(b.GetAvailable() >= nbytes);
	b.Consume(nbytes);

	if (b.empty())
		buffers.pop_front();
}

void
MultiFifoBuffer::FillBucketList(IstreamBucketList &list) const noexcept
{
	for (const auto &i : buffers)
		list.Push(i.Read().ToVoid());
}

size_t
MultiFifoBuffer::Skip(size_t nbytes) noexcept
{
	size_t result = 0;
	while (!empty()) {
		auto &b = buffers.front();
		const size_t available = b.GetAvailable();
		size_t consume = std::min(nbytes, available);
		result += consume;
		nbytes -= consume;
		if (consume < available) {
			b.Consume(consume);
			break;
		}

		buffers.pop_front();
	}

	return result;
}
