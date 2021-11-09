/*
 * Copyright 2007-2021 CM4all GmbH
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
FilteredSocket::OnBufferedRemaining(size_t remaining) noexcept
{
	return filter->OnRemaining(remaining);
}

bool
FilteredSocket::OnBufferedWrite()
{
	return filter->InternalWrite();
}

bool
FilteredSocket::OnBufferedEnd() noexcept
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
		     Event::Duration read_timeout,
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
		  read_timeout, write_timeout,
		  *_handler);

#ifndef NDEBUG
	ended = false;
#endif

	drained = true;

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
		base.Init(fd, fd_type,
			  Event::Duration{-1}, Event::Duration{-1},
			  *this);
	else
		base.Init(fd, fd_type);

#ifndef NDEBUG
	ended = false;
#endif

	drained = true;

	if (filter != nullptr)
		filter->Init(*this);
}

void
FilteredSocket::Reinit(Event::Duration read_timeout,
		       Event::Duration write_timeout,
		       BufferedSocketHandler &_handler) noexcept
{
	if (filter != nullptr) {
		handler = &_handler;
		base.SetTimeouts(read_timeout, write_timeout);
	} else
		base.Reinit(read_timeout, write_timeout, _handler);
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

size_t
FilteredSocket::GetAvailable() const noexcept
{
	return filter != nullptr
		? filter->GetAvailable()
		: base.GetAvailable();
}

WritableBuffer<void>
FilteredSocket::ReadBuffer() const noexcept
{
	return filter != nullptr
		? filter->ReadBuffer()
		: base.ReadBuffer();
}

void
FilteredSocket::DisposeConsumed(size_t nbytes) noexcept
{
	if (filter != nullptr)
		filter->Consumed(nbytes);
	else
		base.DisposeConsumed(nbytes);
}

bool
FilteredSocket::Read(bool expect_more) noexcept
{
	if (filter != nullptr)
		return filter->Read(expect_more);
	else
		return base.Read(expect_more);
}

ssize_t
FilteredSocket::Write(const void *data, size_t length) noexcept
{
	return filter != nullptr
		? filter->Write(data, length)
		: base.Write(data, length);
}

bool
FilteredSocket::InternalDrained() noexcept
{
	assert(filter != nullptr);
	assert(IsConnected());

	if (drained)
		return true;

	drained = true;
	return handler->OnBufferedDrained();
}

bool
FilteredSocket::InvokeTimeout() noexcept
{
	return handler->OnBufferedTimeout();
}
