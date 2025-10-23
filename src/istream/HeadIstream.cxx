// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "HeadIstream.hxx"
#include "ForwardIstream.hxx"
#include "UnusedPtr.hxx"
#include "Bucket.hxx"
#include "New.hxx"
#include "io/FileDescriptor.hxx"

#include <algorithm>

#include <assert.h>

class HeadIstream final : public ForwardIstream {
	uint_least64_t rest;
	const bool authoritative;

public:
	HeadIstream(struct pool &p, UnusedIstreamPtr _input,
		    std::size_t size, bool _authoritative) noexcept
		:ForwardIstream(p, std::move(_input)),
		 rest(size), authoritative(_authoritative) {}

	/* virtual methods from class Istream */

	IstreamLength _GetLength() noexcept override;
	ConsumeBucketResult _ConsumeBucketList(std::size_t nbytes) noexcept override;
	void _ConsumeDirect(std::size_t nbytes) noexcept override;
	off_t _Skip(off_t length) noexcept override;
	void _Read() noexcept override;

	void _FillBucketList(IstreamBucketList &list) override;

	/* virtual methods from class IstreamHandler */
	std::size_t OnData(std::span<const std::byte> src) noexcept override;
	IstreamDirectResult OnDirect(FdType type, FileDescriptor fd,
				     off_t offset, std::size_t max_length,
				     bool then_eof) noexcept override;
};

/*
 * istream handler
 *
 */

std::size_t
HeadIstream::OnData(std::span<const std::byte> src) noexcept
{
	if (rest == 0) {
		DestroyEof();
		return 0;
	}

	if (std::cmp_greater(src.size(), rest))
		src = src.first(rest);

	std::size_t nbytes = InvokeData(src);
	assert(nbytes == 0 || std::cmp_less_equal(nbytes, rest));

	if (nbytes > 0) {
		rest -= nbytes;
		if (rest == 0) {
			DestroyEof();
			return 0;
		}
	}

	return nbytes;
}

void
HeadIstream::_FillBucketList(IstreamBucketList &list)
{
	if (rest == 0)
		return;

	IstreamBucketList tmp1;
	ForwardIstream::_FillBucketList(tmp1);

	const auto nbytes = list.SpliceBuffersFrom(std::move(tmp1), rest, false);
	if (std::cmp_less(nbytes, rest) && tmp1.HasMore()) {
		list.SetMore();

		if (tmp1.ShouldFallback())
			list.EnableFallback();
	}
}

Istream::ConsumeBucketResult
HeadIstream::_ConsumeBucketList(std::size_t nbytes) noexcept
{
	if (std::cmp_greater(nbytes, rest))
		nbytes = rest;

	auto r = ForwardIstream::_ConsumeBucketList(nbytes);
	rest -= nbytes;
	r.eof = rest == 0;
	return r;
}

void
HeadIstream::_ConsumeDirect(std::size_t nbytes) noexcept
{
	assert(std::cmp_less_equal(nbytes, rest));

	rest -= nbytes;
	ForwardIstream::_ConsumeDirect(nbytes);
}

IstreamDirectResult
HeadIstream::OnDirect(FdType type, FileDescriptor fd, off_t offset,
		      std::size_t max_length, bool then_eof) noexcept
{
	if (rest == 0) {
		DestroyEof();
		return IstreamDirectResult::CLOSED;
	}

	if (std::cmp_greater(max_length, rest)) {
		max_length = rest;
		then_eof = true;
	}

	const auto result = InvokeDirect(type, fd, offset, max_length, then_eof);

	if (result == IstreamDirectResult::OK && rest == 0) {
		DestroyEof();
		return IstreamDirectResult::CLOSED;
	}

	return result;
}

/*
 * istream implementation
 *
 */

IstreamLength
HeadIstream::_GetLength() noexcept
{
	if (authoritative) {
#ifndef NDEBUG
		const auto from_input = input.GetLength();
		assert(!from_input.exhaustive || from_input.length >= rest);
#endif

		return {.length = rest, .exhaustive = true};
	}

	auto result = input.GetLength();
	if (result.length > rest) {
		result.length = rest;
		result.exhaustive = true;
	}

	return result;
}

off_t
HeadIstream::_Skip(off_t length) noexcept
{
	if (std::cmp_greater_equal(length, rest))
		length = rest;

	off_t nbytes = ForwardIstream::_Skip(length);
	assert(nbytes <= length);

	if (nbytes > 0)
		rest -= nbytes;

	return nbytes;
}

void
HeadIstream::_Read() noexcept
{
	if (rest == 0) {
		DestroyEof();
	} else {
		ForwardIstream::_Read();
	}
}

/*
 * constructor
 *
 */

UnusedIstreamPtr
istream_head_new(struct pool &pool, UnusedIstreamPtr input,
		 std::size_t size, bool authoritative) noexcept
{
	return NewIstreamPtr<HeadIstream>(pool, std::move(input),
					  size, authoritative);
}
