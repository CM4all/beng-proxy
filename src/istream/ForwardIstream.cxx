/*
 * Copyright 2007-2022 CM4all GmbH
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

int
ForwardIstream::_AsFd() noexcept
{
	int fd = input.AsFd();
	if (fd >= 0)
		Destroy();
	return fd;
}

std::size_t
ForwardIstream::OnData(const void *data, std::size_t length) noexcept
{
	return InvokeData(data, length);
}

IstreamDirectResult
ForwardIstream::OnDirect(FdType type, FileDescriptor fd,
			 std::size_t max_length) noexcept
{
	return InvokeDirect(type, fd, max_length);
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
