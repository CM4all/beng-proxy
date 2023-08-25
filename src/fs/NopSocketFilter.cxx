// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

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

std::size_t
NopSocketFilter::GetAvailable() const noexcept
{
	return socket->InternalGetAvailable();
}

std::span<std::byte>
NopSocketFilter::ReadBuffer() noexcept
{
	return socket->InternalReadBuffer();
}

void
NopSocketFilter::Consumed(std::size_t nbytes) noexcept
{
	socket->InternalConsumed(nbytes);
}

void
NopSocketFilter::AfterConsumed() noexcept
{
	socket->InternalAfterConsumed();
}

BufferedReadResult
NopSocketFilter::Read() noexcept
{
	return socket->InternalRead();
}

ssize_t
NopSocketFilter::Write(std::span<const std::byte> src) noexcept
{
	return socket->InternalWrite(src);
}

void
NopSocketFilter::ScheduleRead() noexcept
{
	socket->InternalScheduleRead();
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
NopSocketFilter::OnRemaining(std::size_t remaining) noexcept
{
	return socket->InvokeRemaining(remaining);
}

void
NopSocketFilter::OnEnd()
{
	socket->InvokeEnd();
}

void
NopSocketFilter::Close() noexcept
{
	delete this;
}
