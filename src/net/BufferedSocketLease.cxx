// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "BufferedSocketLease.hxx"
#include "memory/fb_pool.hxx"
#include "net/SocketProtocolError.hxx"

BufferedSocketLease::BufferedSocketLease(BufferedSocket &_socket, Lease &lease,
					 Event::Duration write_timeout,
					 BufferedSocketHandler &_handler) noexcept
	:socket(&_socket), lease_ref(lease),
	 handler(_handler)
{
	socket->Reinit(write_timeout, *this);
}

BufferedSocketLease::~BufferedSocketLease() noexcept
{
	assert(IsReleased());
}

inline void
BufferedSocketLease::MoveSocketInput() noexcept
{
	assert(input.empty());

	input.MoveFromAllowBothNull(socket->GetInputBuffer());
	assert(socket->GetAvailable() == 0);
}

void
BufferedSocketLease::Release(bool preserve, PutAction action) noexcept
{
	assert(!IsReleased());
	assert(lease_ref);

	if (preserve)
		MoveSocketInput();

	socket->SetDirect(false);

	action = lease_ref.Release(action);
	if (handler_info != nullptr) {
#ifndef NDEBUG
		handler_info->released = true;
#endif
		handler_info->action = action;
	}

	socket = nullptr;
}

bool
BufferedSocketLease::IsEmpty() const noexcept
{
	if (IsReleased())
		return IsReleasedEmpty();
	else
		return socket->IsEmpty();
}

std::size_t
BufferedSocketLease::GetAvailable() const noexcept
{
	if (IsReleased())
		return input.GetAvailable();
	else
		return socket->GetAvailable();
}

std::span<std::byte>
BufferedSocketLease::ReadBuffer() const noexcept
{
	return IsReleased()
		? input.Read()
		: socket->ReadBuffer();
}

void
BufferedSocketLease::DisposeConsumed(std::size_t nbytes) noexcept
{
	if (IsReleased())
		input.Consume(nbytes);
	else
		socket->DisposeConsumed(nbytes);
}

void
BufferedSocketLease::AfterConsumed() noexcept
{
	if (!IsReleased())
		socket->AfterConsumed();
}

bool
BufferedSocketLease::ReadReleased() noexcept
{
	const std::size_t remaining = input.GetAvailable();
	if (remaining == 0)
		return true;

	switch (handler.OnBufferedData()) {
	case BufferedResult::OK:
		if (IsReleasedEmpty()) {
			try {
				if (!handler.OnBufferedEnd())
					return false;
			} catch (...) {
				handler.OnBufferedError(std::current_exception());
				return false;
			}
		}

		if (input.GetAvailable() >= remaining)
			/* no data was consumed */
			return true;

		break;

	case BufferedResult::MORE:
		if (IsReleasedEmpty()) {
			handler.OnBufferedError(std::make_exception_ptr(SocketClosedPrematurelyError{}));
			return false;
		}

		break;

	case BufferedResult::AGAIN:
		break;

	case BufferedResult::DESTROYED:
		return false;
	}

	return true;
}

BufferedReadResult
BufferedSocketLease::Read() noexcept
{
	if (IsReleased())
		return ReadReleased()
			? BufferedReadResult::DISCONNECTED
			: BufferedReadResult::DESTROYED;

	const DestructObserver destructed{destruct_anchor};

	auto result = socket->Read();

	if (result == BufferedReadResult::DESTROYED && !destructed) {
		/* BufferedSocket::Read() may return DESTROYED if we
		   have just released our lease, but this lease has
		   not been destroyed: translate the return value ot
		   DISCONNECTED instead */
		assert(IsReleased());
		result = BufferedReadResult::DISCONNECTED;
	} else if (destructed)
		/* the BufferedSocket is alive, but the lease has been
		   destroyed */
		result = BufferedReadResult::DESTROYED;

	return result;
}

BufferedResult
BufferedSocketLease::OnBufferedData()
{
	HandlerInfo info;

	while (true) {
		assert(handler_info == nullptr);
		handler_info = &info;

		const auto result = handler.OnBufferedData();

		if (result != BufferedResult::DESTROYED) {
			assert(handler_info == &info);
			handler_info = nullptr;
		}

		if (result == BufferedResult::DESTROYED) {
			assert(info.released);

			return info.action == PutAction::DESTROY
				? BufferedResult::DESTROYED
				/* the BufferedSocketLease was
				   destroyed, but the BufferedSocket
				   is still alive (in the
				   BufferedSocketStock) */
				: BufferedResult::OK;
		}

		if (!IsReleased())
			return result;

		assert(info.released);

		/* since the BufferedSocket is gone already, we must handle
		   the AGAIN result codes here */

		if (result != BufferedResult::AGAIN && !IsEmpty()) {
			/* if the socket has been released, we must
			   always report OK/DESTROYED to the released
			   BufferedSocket instance, even if our
			   handler still wants to consume the
			   remaining buffer */
			return info.action == PutAction::DESTROY
				? BufferedResult::DESTROYED
				: BufferedResult::OK;
		}
	}
}

DirectResult
BufferedSocketLease::OnBufferedDirect(SocketDescriptor fd, FdType fd_type)
{
	assert(handler_info == nullptr);
	HandlerInfo info;
	handler_info = &info;

	auto result = handler.OnBufferedDirect(fd, fd_type);

	if (result != DirectResult::CLOSED) {
		assert(handler_info == &info);
		handler_info = nullptr;
	}

	if (result == DirectResult::CLOSED) {
		assert(info.released);

		if (info.action != PutAction::DESTROY)
			/* the BufferedSocketLease was destroyed, but
			   the BufferedSocket is still alive (in the
			   BufferedSocketStock) */
			result = DirectResult::OK;
	}

	return result;
}

bool
BufferedSocketLease::OnBufferedHangup() noexcept
{
	return handler.OnBufferedHangup();
}

bool
BufferedSocketLease::OnBufferedClosed() noexcept
{
	auto result = handler.OnBufferedClosed();
	if (result && IsReleased()) {
		result = false;

		if (handler.OnBufferedRemaining(GetAvailable()) &&
		    ReadReleased() &&
		    IsReleasedEmpty()) {
			try {
				if (!handler.OnBufferedEnd())
					return false;
			} catch (...) {
				handler.OnBufferedError(std::current_exception());
				return false;
			}
		}
	}

	return result;
}

bool
BufferedSocketLease::OnBufferedRemaining(std::size_t remaining) noexcept
{
	auto result = handler.OnBufferedRemaining(remaining);
	if (result && IsReleased())
		result = false;
	return result;
}

bool
BufferedSocketLease::OnBufferedEnd()
{
	return handler.OnBufferedEnd();
}

bool
BufferedSocketLease::OnBufferedWrite()
{
	return handler.OnBufferedWrite();
}

bool
BufferedSocketLease::OnBufferedDrained() noexcept
{
	return handler.OnBufferedDrained();
}

bool
BufferedSocketLease::OnBufferedTimeout() noexcept
{
	return handler.OnBufferedTimeout();
}

enum write_result
BufferedSocketLease::OnBufferedBroken() noexcept
{
	return handler.OnBufferedBroken();
}

void
BufferedSocketLease::OnBufferedError(std::exception_ptr e) noexcept
{
	return handler.OnBufferedError(e);
}
