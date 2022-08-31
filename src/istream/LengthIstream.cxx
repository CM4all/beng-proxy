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

#include "LengthIstream.hxx"

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

std::size_t
LengthIstream::_ConsumeBucketList(std::size_t nbytes) noexcept
{
	auto consumed = input.ConsumeBucketList(nbytes);
	remaining -= consumed;
	return consumed;
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
