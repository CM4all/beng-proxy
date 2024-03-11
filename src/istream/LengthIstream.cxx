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

	const bool has_non_buffer = tmp.HasNonBuffer();
	const bool maybe_more = tmp.HasMore() || has_non_buffer;
	const std::size_t size = tmp.GetTotalBufferSize();

	if (std::cmp_greater(size, remaining)) {
		Destroy();
		throw std::runtime_error{"Too much data in stream"};
	}

	if (!maybe_more && std::cmp_less(size, remaining)) {
		Destroy();
		throw std::runtime_error{"Premature end of stream"};
	}

	list.SpliceBuffersFrom(std::move(tmp));

	if (tmp.HasMore() && !has_non_buffer && std::cmp_equal(size, remaining)) {
		/* our input isn't yet sure whether it has ended, but
		   since we got just the right amount of data, let's
		   pretend it's the end */
		list.DisableFallback();
		list.SetMore(false);
	}
}

Istream::ConsumeBucketResult
LengthIstream::_ConsumeBucketList(std::size_t nbytes) noexcept
{
	if (std::cmp_greater(nbytes, remaining))
		nbytes = remaining;

	if (nbytes == 0)
		return {0, remaining == 0};

	auto r = input.ConsumeBucketList(nbytes);
	remaining -= r.consumed;
	Consumed(r.consumed);

	if (remaining == 0)
		/* even if our input has not yet reported end-of-file,
		   we'll override that; maybe the input doesn't have
		   enough information yet, but since _GetAvailable()
		   returned an authoritative value, we must stay
		   consistent with that */
		r.eof = true;

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
	if (std::cmp_greater(src.size(), remaining)) {
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
