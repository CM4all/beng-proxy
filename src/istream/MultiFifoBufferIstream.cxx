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

#include "MultiFifoBufferIstream.hxx"
#include "Bucket.hxx"
#include "SliceFifoBuffer.hxx"
#include "fb_pool.hxx"

#include <string.h>

void
MultiFifoBufferIstream::Push(ConstBuffer<void> src) noexcept
{
	buffer.Push(ConstBuffer<uint8_t>::FromVoid(src));
}

void
MultiFifoBufferIstream::SetEof() noexcept
{
	eof = true;
	SubmitBuffer();
}

void
MultiFifoBufferIstream::SubmitBuffer() noexcept
{
	size_t consumed = 0;

	while (!buffer.empty()) {
		size_t nbytes = SendFromBuffer(buffer);
		if (nbytes == 0)
			return;

		if (!eof)
			/* we need to call this in every iteration,
			   because nbytes==0 may mean that this object
			   has been destroyed */
			handler.OnFifoBufferIstreamConsumed(nbytes);

		consumed += nbytes;
	}

	if (eof)
		DestroyEof();
}

off_t
MultiFifoBufferIstream::_Skip(off_t length) noexcept
{
	size_t consumed = buffer.Skip(length);

	if (consumed > 0) {
		Consumed(consumed);

		if (!eof)
			handler.OnFifoBufferIstreamConsumed(consumed);
	}

	return consumed;
}

void
MultiFifoBufferIstream::_Read() noexcept
{
	SubmitBuffer();
}

void
MultiFifoBufferIstream::_FillBucketList(IstreamBucketList &list) noexcept
{
	buffer.FillBucketList(list);
}

size_t
MultiFifoBufferIstream::_ConsumeBucketList(size_t nbytes) noexcept
{
	size_t consumed = buffer.Skip(nbytes);

	if (consumed > 0) {
		Consumed(consumed);

		if (!eof)
			handler.OnFifoBufferIstreamConsumed(consumed);
	}

	return consumed;
}

void
MultiFifoBufferIstream::_Close() noexcept
{
	if (!eof)
		handler.OnFifoBufferIstreamClosed();
	Istream::_Close();
}
