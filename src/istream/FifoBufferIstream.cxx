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

#include "FifoBufferIstream.hxx"
#include "Bucket.hxx"
#include "fb_pool.hxx"

#include <string.h>

size_t
FifoBufferIstream::Push(ConstBuffer<void> src) noexcept
{
	buffer.AllocateIfNull(fb_pool_get());

	auto w = buffer.Write();
	size_t nbytes = std::min(w.size, src.size);
	memcpy(w.data, src.data, nbytes);
	buffer.Append(nbytes);
	return nbytes;
}

void
FifoBufferIstream::SetEof() noexcept
{
	eof = true;

	if (buffer.empty())
		DestroyEof();
	else
		SubmitBuffer();
}

void
FifoBufferIstream::SubmitBuffer() noexcept
{
	while (!buffer.empty()) {
		size_t nbytes = SendFromBuffer(buffer);
		if (nbytes == 0)
			return;

		handler.OnFifoBufferIstreamConsumed(nbytes);

		if (buffer.empty() && !eof)
			handler.OnFifoBufferIstreamDrained();
	}

	if (buffer.empty()) {
		if (eof)
			DestroyEof();
		else
			buffer.FreeIfDefined();
	}
}

off_t
FifoBufferIstream::_Skip(off_t length) noexcept
{
	size_t nbytes = std::min<decltype(length)>(length, buffer.GetAvailable());
	buffer.Consume(nbytes);
	buffer.FreeIfEmpty();
	Consumed(nbytes);
	handler.OnFifoBufferIstreamConsumed(nbytes);
	return nbytes;
}

void
FifoBufferIstream::_Read() noexcept
{
	SubmitBuffer();
}

void
FifoBufferIstream::_FillBucketList(IstreamBucketList &list) noexcept
{
	auto r = buffer.Read();
	if (!r.empty())
		list.Push(r.ToVoid());

	if (!eof)
		list.SetMore();
}

size_t
FifoBufferIstream::_ConsumeBucketList(size_t nbytes) noexcept
{
	size_t consumed = std::min(nbytes, buffer.GetAvailable());
	buffer.Consume(consumed);
	Consumed(nbytes);
	handler.OnFifoBufferIstreamConsumed(consumed);

	if (consumed > 0 && buffer.empty() && !eof) {
		handler.OnFifoBufferIstreamDrained();

		if (buffer.empty())
			buffer.Free();
	}

	return nbytes - consumed;
}
