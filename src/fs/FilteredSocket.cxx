// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "FilteredSocket.hxx"
#include "net/UniqueSocketDescriptor.hxx"

#include <utility>

#include <string.h>

FilteredSocket::FilteredSocket(EventLoop &_event_loop,
			       UniqueSocketDescriptor _fd, FdType _fd_type,
			       SocketFilterPtr _filter)
	:FilteredSocket(_event_loop)
{
	InitDummy(_fd.Release(), _fd_type, std::move(_filter));
}

FilteredSocket::~FilteredSocket() noexcept
{
	if (IsValid()) {
		if (IsConnected())
			Close();
		Destroy();
	}
}

/*
 * buffered_socket_handler
 *
 */

BufferedResult
FilteredSocket::OnBufferedData()
{
	return filter->OnData();
}

bool
FilteredSocket::OnBufferedHangup() noexcept
{
	return handler->OnBufferedHangup();
}

bool
FilteredSocket::OnBufferedClosed() noexcept
{
	return InvokeClosed();
}

bool
FilteredSocket::OnBufferedRemaining(std::size_t remaining) noexcept
{
	return filter->OnRemaining(remaining);
}

bool
FilteredSocket::OnBufferedWrite()
{
	return filter->InternalWrite();
}

bool
FilteredSocket::OnBufferedEnd()
{
	filter->OnEnd();
	return true;
}

bool
FilteredSocket::OnBufferedTimeout() noexcept
{
	// TODO: let handler intercept this call
	return InvokeTimeout();
}

enum write_result
FilteredSocket::OnBufferedBroken() noexcept
{
	return handler->OnBufferedBroken();
}

void
FilteredSocket::OnBufferedError(std::exception_ptr ep) noexcept
{
	handler->OnBufferedError(ep);
}

/*
 * constructor
 *
 */

void
FilteredSocket::Init(SocketDescriptor fd, FdType fd_type,
		     Event::Duration write_timeout,
		     SocketFilterPtr _filter,
		     BufferedSocketHandler &__handler) noexcept
{
	BufferedSocketHandler *_handler = &__handler;

	filter = std::move(_filter);

	if (filter != nullptr) {
		handler = _handler;

		_handler = this;
	}

	base.Init(fd, fd_type,
		  write_timeout,
		  *_handler);

#ifndef NDEBUG
	ended = false;
#endif

	drained = true;
	shutting_down = false;

	if (filter != nullptr)
		filter->Init(*this);
}

void
FilteredSocket::InitDummy(SocketDescriptor fd, FdType fd_type,
			  SocketFilterPtr _filter) noexcept
{
	assert(!filter);

	filter = std::move(_filter);

	if (filter != nullptr)
		base.Init(fd, fd_type, Event::Duration{-1}, *this);
	else
		base.Init(fd, fd_type);

#ifndef NDEBUG
	ended = false;
#endif

	drained = true;
	shutting_down = false;

	if (filter != nullptr)
		filter->Init(*this);
}

void
FilteredSocket::Reinit(Event::Duration write_timeout,
		       BufferedSocketHandler &_handler) noexcept
{
	if (filter != nullptr) {
		handler = &_handler;
		base.SetWriteTimeout(write_timeout);
	} else
		base.Reinit(write_timeout, _handler);
}

void
FilteredSocket::Destroy() noexcept
{
	filter.reset();
	base.Destroy();
}

bool
FilteredSocket::IsEmpty() const noexcept
{
	return filter != nullptr
		? filter->IsEmpty()
		: base.IsEmpty();
}

bool
FilteredSocket::IsFull() const noexcept
{
	return filter != nullptr
		? filter->IsFull()
		: base.IsFull();
}

std::size_t
FilteredSocket::GetAvailable() const noexcept
{
	return filter != nullptr
		? filter->GetAvailable()
		: base.GetAvailable();
}

std::span<std::byte>
FilteredSocket::ReadBuffer() const noexcept
{
	return filter != nullptr
		? filter->ReadBuffer()
		: base.ReadBuffer();
}

void
FilteredSocket::DisposeConsumed(std::size_t nbytes) noexcept
{
	if (filter != nullptr)
		filter->Consumed(nbytes);
	else
		base.DisposeConsumed(nbytes);
}

void
FilteredSocket::AfterConsumed() noexcept
{
	if (filter != nullptr)
		filter->AfterConsumed();
	else
		base.AfterConsumed();
}

BufferedReadResult
FilteredSocket::Read() noexcept
{
	if (filter != nullptr)
		return filter->Read();
	else
		return base.Read();
}

ssize_t
FilteredSocket::Write(std::span<const std::byte> src) noexcept
{
	return filter != nullptr
		? filter->Write(src)
		: base.Write(src);
}

bool
FilteredSocket::InternalDrained() noexcept
{
	assert(filter != nullptr);
	assert(IsConnected());

	if (drained)
		return true;

	drained = true;

	if (shutting_down)
		base.Shutdown();

	return handler->OnBufferedDrained();
}

bool
FilteredSocket::InvokeTimeout() noexcept
{
	return handler->OnBufferedTimeout();
}
