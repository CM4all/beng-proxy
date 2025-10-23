// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "ForwardIstream.hxx"
#include "io/FileDescriptor.hxx"

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
ForwardIstream::OnError(std::exception_ptr &&error) noexcept
{
	ClearInput();
	DestroyError(std::move(error));
}
