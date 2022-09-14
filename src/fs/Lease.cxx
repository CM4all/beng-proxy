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

#include "Lease.hxx"
#include "memory/fb_pool.hxx"
#include "net/SocketProtocolError.hxx"

FilteredSocketLease::FilteredSocketLease(FilteredSocket &_socket, Lease &lease,
					 Event::Duration write_timeout,
					 BufferedSocketHandler &_handler) noexcept
	:socket(&_socket), lease_ref(lease),
	 handler(_handler)
{
	socket->Reinit(write_timeout, *this);
}

FilteredSocketLease::~FilteredSocketLease() noexcept
{
	assert(IsReleased());
}

void
FilteredSocketLease::MoveSocketInput() noexcept
{
	// TODO: move buffers instead of copying the data
	std::size_t i = 0;
	while (true) {
		auto r = socket->ReadBuffer();
		if (r.empty())
			break;

		auto &dest = input[i];
		if (!dest.IsDefined())
			dest.Allocate(fb_pool_get());
		else if (dest.IsFull()) {
			++i;
			assert(i < input.size());
			continue;
		}

		std::size_t n = dest.MoveFrom(r);
		assert(n > 0);
		socket->DisposeConsumed(n);
	}
}

void
FilteredSocketLease::Release(bool preserve, bool reuse) noexcept
{
	assert(!IsReleased());
	assert(lease_ref);

	if (preserve)
		MoveSocketInput();

	lease_ref.Release(reuse);
	socket = nullptr;
}

bool
FilteredSocketLease::IsEmpty() const noexcept
{
	if (IsReleased())
		return IsReleasedEmpty();
	else
		return socket->IsEmpty();
}

std::size_t
FilteredSocketLease::GetAvailable() const noexcept
{
	if (IsReleased()) {
		std::size_t result = 0;
		for (const auto &i : input)
			result += i.GetAvailable();
		return result;
	} else
		return socket->GetAvailable();
}

std::span<std::byte>
FilteredSocketLease::ReadBuffer() const noexcept
{
	return IsReleased()
		? std::span<std::byte>{input.front().Read()}
		: socket->ReadBuffer();
}

void
FilteredSocketLease::DisposeConsumed(std::size_t nbytes) noexcept
{
	if (IsReleased()) {
		input.front().Consume(nbytes);
		MoveInput();
	} else
		socket->DisposeConsumed(nbytes);
}

void
FilteredSocketLease::AfterConsumed() noexcept
{
	if (!IsReleased())
		socket->AfterConsumed();
}

bool
FilteredSocketLease::ReadReleased() noexcept
{
	while (!IsReleasedEmpty()) {
		const std::size_t remaining = input.front().GetAvailable();

		switch (handler.OnBufferedData()) {
		case BufferedResult::OK:
			if (IsReleasedEmpty() && !handler.OnBufferedEnd())
				return false;

			if (input.front().GetAvailable() >= remaining)
				/* no data was consumed */
				return true;

			break;

		case BufferedResult::MORE:
			handler.OnBufferedError(std::make_exception_ptr(SocketClosedPrematurelyError()));
			return false;

		case BufferedResult::AGAIN:
			break;

		case BufferedResult::CLOSED:
			return false;
		}
	}

	return true;
}

bool
FilteredSocketLease::Read() noexcept
{
	if (IsReleased())
		return ReadReleased();
	else
		return socket->Read();
}

void
FilteredSocketLease::MoveInput() noexcept
{
	auto &dest = input.front();
	for (std::size_t i = 1; !dest.IsFull() && i < input.size(); ++i) {
		auto &src = input[i];
		dest.MoveFromAllowBothNull(src);
		src.FreeIfEmpty();
	}
}

BufferedResult
FilteredSocketLease::OnBufferedData()
{
	while (true) {
		const auto result = handler.OnBufferedData();
		if (result == BufferedResult::CLOSED)
			break;

		if (!IsReleased())
			return result;

		/* since the BufferedSocket is gone already, we must handle
		   the AGAIN result codes here */

		if (result != BufferedResult::AGAIN && !IsEmpty())
			break;
	}

	/* if the socket has been released, we must always report CLOSED
	   to the released BufferedSocket instance, even if our handler
	   still wants to consume the remaining buffer */
	return BufferedResult::CLOSED;
}

DirectResult
FilteredSocketLease::OnBufferedDirect(SocketDescriptor fd, FdType fd_type)
{
	return handler.OnBufferedDirect(fd, fd_type);
}

bool
FilteredSocketLease::OnBufferedHangup() noexcept
{
	return handler.OnBufferedHangup();
}

bool
FilteredSocketLease::OnBufferedClosed() noexcept
{
	auto result = handler.OnBufferedClosed();
	if (result && IsReleased()) {
		result = false;

		if (handler.OnBufferedRemaining(GetAvailable()) &&
		    ReadReleased() &&
		    IsReleasedEmpty() &&
		    !handler.OnBufferedEnd())
			handler.OnBufferedError(std::make_exception_ptr(SocketClosedPrematurelyError()));
	}

	return result;
}

bool
FilteredSocketLease::OnBufferedRemaining(std::size_t remaining) noexcept
{
	auto result = handler.OnBufferedRemaining(remaining);
	if (result && IsReleased())
		result = false;
	return result;
}

bool
FilteredSocketLease::OnBufferedEnd() noexcept
{
	return handler.OnBufferedEnd();
}

bool
FilteredSocketLease::OnBufferedWrite()
{
	return handler.OnBufferedWrite();
}

bool
FilteredSocketLease::OnBufferedDrained() noexcept
{
	return handler.OnBufferedDrained();
}

bool
FilteredSocketLease::OnBufferedTimeout() noexcept
{
	return handler.OnBufferedTimeout();
}

enum write_result
FilteredSocketLease::OnBufferedBroken() noexcept
{
	return handler.OnBufferedBroken();
}

void
FilteredSocketLease::OnBufferedError(std::exception_ptr e) noexcept
{
	return handler.OnBufferedError(e);
}
