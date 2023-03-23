// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "FifoBufferIstream.hxx"
#include "Bucket.hxx"
#include "memory/fb_pool.hxx"

size_t
FifoBufferIstream::Push(std::span<const std::byte> src) noexcept
{
	buffer.AllocateIfNull(fb_pool_get());
	return buffer.MoveFrom(src);
}

void
FifoBufferIstream::SetEof() noexcept
{
	eof = true;
	SubmitBuffer();
}

void
FifoBufferIstream::SubmitBuffer() noexcept
{
	while (!buffer.empty()) {
		size_t nbytes = SendFromBuffer(buffer);
		if (nbytes == 0)
			return;

		if (!eof) {
			handler.OnFifoBufferIstreamConsumed(nbytes);
			if (buffer.empty())
				handler.OnFifoBufferIstreamDrained();
		}
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

	if (nbytes > 0 && !eof) {
		handler.OnFifoBufferIstreamConsumed(nbytes);
		if (buffer.empty())
			handler.OnFifoBufferIstreamDrained();
	}

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
		list.Push(r);

	if (!eof)
		list.SetMore();
}

Istream::ConsumeBucketResult
FifoBufferIstream::_ConsumeBucketList(size_t nbytes) noexcept
{
	size_t consumed = std::min(nbytes, buffer.GetAvailable());
	buffer.Consume(consumed);
	Consumed(consumed);

	if (consumed > 0 && !eof) {
		handler.OnFifoBufferIstreamConsumed(consumed);
		if (buffer.empty())
			handler.OnFifoBufferIstreamDrained();

		if (buffer.empty())
			buffer.Free();
	}

	return {consumed, eof && buffer.empty()};
}
