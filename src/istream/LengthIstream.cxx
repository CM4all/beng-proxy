// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "LengthIstream.hxx"
#include "Bucket.hxx"

#include <stdexcept>

off_t
LengthIstream::_GetAvailable(bool) noexcept
{
	return remaining;
}

off_t
LengthIstream::_Skip(off_t length) noexcept
{
	off_t nbytes = ForwardIstream::_Skip(length);
	if (nbytes > 0)
		remaining -= nbytes;
	return nbytes;
}

void
LengthIstream::_FillBucketList(IstreamBucketList &list)
{
	IstreamBucketList tmp;
	FillBucketListFromInput(tmp);

	const bool maybe_more = tmp.HasMore() || tmp.HasNonBuffer();
	const std::size_t size = tmp.GetTotalBufferSize();

	if ((off_t)size > remaining) {
		Destroy();
		throw std::runtime_error{"Too much data in stream"};
	}

	if (!maybe_more && (off_t)size < remaining) {
		Destroy();
		throw std::runtime_error{"Premature end of stream"};
	}

	list.SpliceBuffersFrom(std::move(tmp));
}

Istream::ConsumeBucketResult
LengthIstream::_ConsumeBucketList(std::size_t nbytes) noexcept
{
	auto r = input.ConsumeBucketList(nbytes);
	remaining -= r.consumed;
	Consumed(r.consumed);
	return r;
}

void
LengthIstream::_ConsumeDirect(std::size_t nbytes) noexcept
{
	remaining -= nbytes;
	ForwardIstream::_ConsumeDirect(nbytes);
}

std::size_t
LengthIstream::OnData(std::span<const std::byte> src) noexcept
{
	if ((off_t)src.size() > remaining) {
		DestroyError(std::make_exception_ptr(std::runtime_error("Too much data in stream")));
		return 0;
	}

	std::size_t nbytes = ForwardIstream::OnData(src);
	if (nbytes > 0)
		remaining -= nbytes;
	return nbytes;
}

void
LengthIstream::OnEof() noexcept
{
	if (remaining == 0)
		ForwardIstream::OnEof();
	else
		DestroyError(std::make_exception_ptr(std::runtime_error("Premature end of stream")));
}
