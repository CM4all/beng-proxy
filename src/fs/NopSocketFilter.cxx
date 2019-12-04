/*
 * Copyright 2007-2018 Content Management AG
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

#include "NopSocketFilter.hxx"
#include "FilteredSocket.hxx"

BufferedResult
NopSocketFilter::OnData() noexcept
{
	return socket->InvokeData();
}

bool
NopSocketFilter::IsEmpty() const noexcept
{
	return socket->InternalIsEmpty();
}

bool
NopSocketFilter::IsFull() const noexcept
{
	return socket->InternalIsFull();
}

size_t
NopSocketFilter::GetAvailable() const noexcept
{
	return socket->InternalGetAvailable();
}

WritableBuffer<void>
NopSocketFilter::ReadBuffer() noexcept
{
	return socket->InternalReadBuffer();
}

void
NopSocketFilter::Consumed(size_t nbytes) noexcept
{
	socket->InternalConsumed(nbytes);
}

bool
NopSocketFilter::Read(bool expect_more) noexcept
{
	return socket->InternalRead(expect_more);
}

ssize_t
NopSocketFilter::Write(const void *data, size_t length) noexcept
{
	return socket->InternalWrite(data, length);
}

void
NopSocketFilter::ScheduleRead(bool expect_more,
			      Event::Duration timeout) noexcept
{
	socket->InternalScheduleRead(expect_more, timeout);
}

void
NopSocketFilter::ScheduleWrite() noexcept
{
	socket->InternalScheduleWrite();
}

void
NopSocketFilter::UnscheduleWrite() noexcept
{
	socket->InternalUnscheduleWrite();
}

bool
NopSocketFilter::InternalWrite() noexcept
{
	return socket->InvokeWrite();
}

bool
NopSocketFilter::OnRemaining(size_t remaining) noexcept
{
	return socket->InvokeRemaining(remaining);
}

void
NopSocketFilter::OnEnd() noexcept
{
	socket->InvokeEnd();
}

void
NopSocketFilter::Close() noexcept
{
}
