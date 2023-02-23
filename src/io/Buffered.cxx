// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Buffered.hxx"
#include "io/FileDescriptor.hxx"
#include "util/ForeignFifoBuffer.hxx"

#include <cassert>

ssize_t
ReadToBuffer(FileDescriptor fd, ForeignFifoBuffer<std::byte> &buffer,
	     std::size_t length) noexcept
{
	assert(fd.IsDefined());

	auto w = buffer.Write();
	if (w.empty())
		return -2;

	if (length > w.size())
		length = w.size();

	ssize_t nbytes = fd.Read(w.data(), length);
	if (nbytes > 0)
		buffer.Append((size_t)nbytes);

	return nbytes;
}

ssize_t
ReadToBufferAt(FileDescriptor fd, off_t offset,
	       ForeignFifoBuffer<std::byte> &buffer,
	       std::size_t length) noexcept
{
	assert(fd.IsDefined());

	auto w = buffer.Write();
	if (w.empty())
		return -2;

	if (length > w.size())
		length = w.size();

	ssize_t nbytes = fd.ReadAt(offset, w.data(), length);
	if (nbytes > 0)
		buffer.Append((size_t)nbytes);

	return nbytes;
}
