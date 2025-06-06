// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

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

inline void
FilteredSocketLease::MoveSocketInput() noexcept
{
	// TODO: move buffers instead of copying the data
	std::size_t i = 0;
	while (true) {
		/* the AfterConsumed() call ensures that
		   ThreadSocketFilter::AfterConsumed() moves the next
		   buffer into place; its
		   "unprotected_decrypted_input" buffer may be empty
		   at this point because the caller of
		   FilteredSocketLease::Release() may have consumed it
		   already, but data in "decrypted_input" remains, but
		   inaccessible to ReadBuffer(); only AfterConsumed()
		   moves it and makes it accessible */
		socket->AfterConsumed();

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

	assert(socket->GetAvailable() == 0);
}

void
FilteredSocketLease::Release(bool preserve, PutAction action) noexcept
{
	assert(!IsReleased());
	assert(lease_ref);

	socket->SetDirect(false);

	if (preserve)
		MoveSocketInput();

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
			if (IsReleasedEmpty()) {
				try {
					if (!handler.OnBufferedEnd())
						return false;
				} catch (...) {
					handler.OnBufferedError(std::current_exception());
					return false;
				}
			}

			if (input.front().GetAvailable() >= remaining)
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
	}

	return true;
}

BufferedReadResult
FilteredSocketLease::Read() noexcept
{
	if (IsReleased())
		return ReadReleased()
			? BufferedReadResult::DISCONNECTED
			: BufferedReadResult::DESTROYED;

	const DestructObserver destructed{destruct_anchor};

	auto result = socket->Read();

	if (result == BufferedReadResult::DESTROYED && !destructed) {
		/* FilteredSocket::Read() may return DESTROYED if we
		   have just released our lease, but this lease has
		   not been destroyed: translate the return value ot
		   DISCONNECTED instead */
		assert(IsReleased());
		result = BufferedReadResult::DISCONNECTED;
	} else if (destructed)
		/* the FilteredSocket is alive, but the lease has been
		   destroyed */
		result = BufferedReadResult::DESTROYED;

	return result;
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
				/* the FilteredSocketLease was
				   destroyed, but the BufferedSocket
				   is still alive (in the
				   FilteredSocketStock) */
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
FilteredSocketLease::OnBufferedDirect(SocketDescriptor fd, FdType fd_type)
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
			/* the FilteredSocketLease was destroyed, but
			   the BufferedSocket is still alive (in the
			   FilteredSocketStock) */
			result = DirectResult::OK;
	}

	return result;
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
FilteredSocketLease::OnBufferedRemaining(std::size_t remaining) noexcept
{
	auto result = handler.OnBufferedRemaining(remaining);
	if (result && IsReleased())
		result = false;
	return result;
}

bool
FilteredSocketLease::OnBufferedEnd()
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
