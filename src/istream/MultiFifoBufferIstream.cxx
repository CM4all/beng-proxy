// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "MultiFifoBufferIstream.hxx"
#include "Bucket.hxx"

void
MultiFifoBufferIstream::SetEof() noexcept
{
	eof = true;
	SubmitBuffer();
}

void
MultiFifoBufferIstream::SubmitBuffer() noexcept
{
	[[maybe_unused]] // do we ever need this variable?
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

	if (!eof)
		list.SetMore();
}

Istream::ConsumeBucketResult
MultiFifoBufferIstream::_ConsumeBucketList(size_t nbytes) noexcept
{
	size_t consumed = buffer.Skip(nbytes);

	if (consumed > 0) {
		Consumed(consumed);

		if (!eof)
			handler.OnFifoBufferIstreamConsumed(consumed);
	}

	return {consumed, eof && buffer.empty()};
}

void
MultiFifoBufferIstream::_Close() noexcept
{
	if (!eof)
		handler.OnFifoBufferIstreamClosed();
	Istream::_Close();
}
