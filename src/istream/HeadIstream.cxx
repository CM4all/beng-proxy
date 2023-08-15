// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "HeadIstream.hxx"
#include "ForwardIstream.hxx"
#include "UnusedPtr.hxx"
#include "Bucket.hxx"
#include "New.hxx"
#include "io/FileDescriptor.hxx"

#include <algorithm>

#include <assert.h>

class HeadIstream final : public ForwardIstream {
	off_t rest;
	const bool authoritative;

public:
	HeadIstream(struct pool &p, UnusedIstreamPtr _input,
		    std::size_t size, bool _authoritative) noexcept
		:ForwardIstream(p, std::move(_input)),
		 rest(size), authoritative(_authoritative) {}

	/* virtual methods from class Istream */

	off_t _GetAvailable(bool partial) noexcept override;
	ConsumeBucketResult _ConsumeBucketList(std::size_t nbytes) noexcept override;
	void _ConsumeDirect(std::size_t nbytes) noexcept override;
	off_t _Skip(off_t length) noexcept override;
	void _Read() noexcept override;

	void _FillBucketList(IstreamBucketList &list) override;

	int _AsFd() noexcept override {
		return -1;
	}

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

	if ((off_t)src.size() > rest)
		src = src.first(rest);

	std::size_t nbytes = InvokeData(src);
	assert((off_t)nbytes <= rest);

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
	if ((off_t)nbytes < rest && tmp1.HasMore()) {
		list.SetMore();

		if (tmp1.ShouldFallback())
			list.EnableFallback();
	}
}

Istream::ConsumeBucketResult
HeadIstream::_ConsumeBucketList(std::size_t nbytes) noexcept
{
	if ((off_t)nbytes > rest)
		nbytes = rest;

	auto r = ForwardIstream::_ConsumeBucketList(nbytes);
	rest -= nbytes;
	r.eof = rest == 0;
	return r;
}

void
HeadIstream::_ConsumeDirect(std::size_t nbytes) noexcept
{
	assert((off_t)nbytes <= rest);

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

	if ((off_t)max_length > rest) {
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

off_t
HeadIstream::_GetAvailable(bool partial) noexcept
{
	if (authoritative) {
		assert(partial ||
		       input.GetAvailable(partial) < 0 ||
		       input.GetAvailable(partial) >= (off_t)rest);
		return rest;
	}

	off_t available = input.GetAvailable(partial);
	return std::min(available, rest);
}

off_t
HeadIstream::_Skip(off_t length) noexcept
{
	if (length >= rest)
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
