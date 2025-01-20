// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "ForwardIstream.hxx"
#include "io/FileDescriptor.hxx"

off_t
ForwardIstream::_Skip(off_t length) noexcept
{
	off_t nbytes = input.Skip(length);
	if (nbytes > 0)
		Consumed(nbytes);
	return nbytes;
}

void
ForwardIstream::_FillBucketList(IstreamBucketList &list)
{
	FillBucketListFromInput(list);
}

std::size_t
ForwardIstream::OnData(std::span<const std::byte> src) noexcept
{
	return InvokeData(src);
}

IstreamDirectResult
ForwardIstream::OnDirect(FdType type, FileDescriptor fd, off_t offset,
			 std::size_t max_length, bool then_eof) noexcept
{
	return InvokeDirect(type, fd, offset, max_length, then_eof);
}

void
ForwardIstream::OnEof() noexcept
{
	ClearInput();
	DestroyEof();
}

void
ForwardIstream::OnError(std::exception_ptr ep) noexcept
{
	ClearInput();
	DestroyError(ep);
}
