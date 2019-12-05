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

#pragma once

#include "SocketFilter.hxx"
#include "Ptr.hxx"
#include "event/net/BufferedSocket.hxx"
#include "util/BindMethod.hxx"

/**
 * A wrapper for #BufferedSocket that can filter input and output.
 *
 * Unlike #BufferedSocket, this class "owns" the socket and closes it
 * automatically in its destructor.
 */
class FilteredSocket final : BufferedSocketHandler {
	BufferedSocket base;

#ifndef NDEBUG
	bool ended;
#endif

	/**
	 * The actual filter.  If this is nullptr, then this object behaves
	 * just like #BufferedSocket.
	 */
	SocketFilterPtr filter;

	BufferedSocketHandler *handler;

	/**
	 * Is there still data in the filter's output?  Once this turns
	 * from "false" to "true", the #BufferedSocket_handler method
	 * drained() will be invoked.
	 */
	bool drained;

public:
	explicit FilteredSocket(EventLoop &_event_loop) noexcept
		:base(_event_loop) {}

	~FilteredSocket() noexcept;

	EventLoop &GetEventLoop() noexcept {
		return base.GetEventLoop();
	}

	void Init(SocketDescriptor fd, FdType fd_type,
		  Event::Duration read_timeout,
		  Event::Duration write_timeout,
		  SocketFilterPtr filter,
		  BufferedSocketHandler &handler) noexcept;

	/**
	 * Initialize a "dummy" instance (without a filter) which cannot
	 * be used to schedule events (because there is no handler); the
	 * next Reinit() call finishes initialization.
	 */
	void InitDummy(SocketDescriptor _fd, FdType _fd_type,
		       SocketFilterPtr _filter={}) noexcept;

	void Reinit(Event::Duration read_timeout,
		    Event::Duration write_timeout,
		    BufferedSocketHandler &handler) noexcept;

	bool HasFilter() const noexcept {
		return filter != nullptr;
	}

	FdType GetType() const noexcept {
		return filter == nullptr
			? base.GetType()
			/* can't do splice() with a filter */
			: FdType::FD_NONE;
	}

	/**
	 * Install a callback that will be invoked as soon as the filter's
	 * protocol "handshake" is complete.  Before this time, no data
	 * transfer is possible.  If the handshake is already complete (or
	 * the filter has no handshake), the callback will be invoked
	 * synchronously by this method.
	 */
	void SetHandshakeCallback(BoundMethod<void() noexcept> callback) noexcept {
		if (filter != nullptr)
			filter->SetHandshakeCallback(callback);
		else
			callback();
	}

	void Shutdown() noexcept {
		base.Shutdown();
	}

	/**
	 * Close the physical socket, but do not destroy the input buffer.  To
	 * do the latter, call filtered_socket_destroy().
	 */
	void Close() noexcept {
		if (filter != nullptr)
			filter->OnClosed();

#ifndef NDEBUG
		/* work around bogus assertion failure */
		if (filter != nullptr && base.HasEnded())
			return;
#endif

		base.Close();
	}

	/**
	 * Just like Close(), but do not actually close the
	 * socket.  The caller is responsible for closing the socket (or
	 * scheduling it for reuse).
	 */
	void Abandon() noexcept {
		if (filter != nullptr)
			filter->OnClosed();

#ifndef NDEBUG
		/* work around bogus assertion failure */
		if (filter != nullptr && base.HasEnded())
			return;
#endif

		base.Abandon();
	}

	bool ClosedByPeer() noexcept {
		return base.ClosedByPeer();
	}

#ifndef NDEBUG
	bool HasEnded() const noexcept {
		return ended;
	}
#endif

	/**
	 * Destroy the object.  Prior to that, the socket must be removed
	 * by calling either filtered_socket_close() or
	 * filtered_socket_abandon().
	 */
	void Destroy() noexcept;

	/**
	 * Returns the socket descriptor and calls Abandon().  Returns -1
	 * if the input buffer is not empty.
	 */
	int AsFD() noexcept {
		return filter != nullptr
			? -1
			: base.AsFD();
	}

	/**
	 * Is the socket still connected?  This does not actually check
	 * whether the socket is connected, just whether it is known to be
	 * closed.
	 */
	bool IsConnected() const noexcept {
#ifndef NDEBUG
		/* work around bogus assertion failure */
		if (filter != nullptr && base.HasEnded())
			return false;
#endif

		return base.IsConnected();
	}

	/**
	 * Is the object still usable?  The socket may be closed already, but
	 * the input buffer may still have data.
	 */
	bool IsValid() const noexcept {
		return base.IsValid();
	}

	/**
	 * Accessor for #drained.
	 */
	bool IsDrained() const noexcept {
		assert(IsValid());

		return drained;
	}

	/**
	 * Is the input buffer empty?
	 */
	gcc_pure
	bool IsEmpty() const noexcept;

	/**
	 * Is the input buffer full?
	 */
	gcc_pure
	bool IsFull() const noexcept;

	/**
	 * Returns the number of bytes in the input buffer.
	 */
	gcc_pure
	size_t GetAvailable() const noexcept;

	WritableBuffer<void> ReadBuffer() const noexcept;

	/**
	 * Dispose the specified number of bytes from the input buffer.
	 * Call this after ReadBuffer().  It may be called repeatedly.
	 */
	void DisposeConsumed(size_t nbytes) noexcept;

	void SetDirect(bool _direct) noexcept {
		assert(!_direct || !HasFilter());

		base.SetDirect(_direct);
	}

	/**
	 * The caller wants to read more data from the socket.  There are
	 * four possible outcomes: a call to filtered_socket_handler.read,
	 * a call to filtered_socket_handler.direct, a call to
	 * filtered_socket_handler.error or (if there is no data available
	 * yet) an event gets scheduled and the function returns
	 * immediately.
	 */
	bool Read(bool expect_more) noexcept;

	ssize_t Write(const void *data, size_t length) noexcept;

	ssize_t WriteV(const struct iovec *v, size_t n) noexcept {
		assert(filter == nullptr);

		return base.WriteV(v, n);
	}

	ssize_t WriteFrom(int fd, FdType fd_type, size_t length) noexcept {
		assert(filter == nullptr);

		return base.WriteFrom(fd, fd_type, length);
	}

	gcc_pure
	bool IsReadyForWriting() const noexcept {
		assert(filter == nullptr);

		return base.IsReadyForWriting();
	}

	void ScheduleReadTimeout(bool expect_more,
				 Event::Duration timeout) noexcept {
		if (filter != nullptr)
			filter->ScheduleRead(expect_more, timeout);
		else
			base.ScheduleReadTimeout(expect_more, timeout);
	}

	/**
	 * Schedules reading on the socket with timeout disabled, to indicate
	 * that you are willing to read, but do not expect it yet.  No direct
	 * action is taken.  Use this to enable reading when you are still
	 * sending the request.  When you are finished sending the request,
	 * you should call filtered_socket_read() to enable the read timeout.
	 */
	void ScheduleReadNoTimeout(bool expect_more) noexcept {
		ScheduleReadTimeout(expect_more, Event::Duration(-1));
	}

	void ScheduleWrite() noexcept {
		if (filter != nullptr)
			filter->ScheduleWrite();
		else
			base.ScheduleWrite();
	}

	void UnscheduleWrite() noexcept {
		if (filter != nullptr)
			filter->UnscheduleWrite();
		else
			base.UnscheduleWrite();
	}

	gcc_pure
	bool InternalIsEmpty() const noexcept {
		assert(filter != nullptr);

		return base.IsEmpty();
	}

	gcc_pure
	bool InternalIsFull() const noexcept {
		assert(filter != nullptr);

		return base.IsFull();
	}

	gcc_pure
	size_t InternalGetAvailable() const noexcept {
		assert(filter != nullptr);

		return base.GetAvailable();
	}

	WritableBuffer<void> InternalReadBuffer() const noexcept {
		assert(filter != nullptr);

		return base.ReadBuffer();
	}

	void InternalConsumed(size_t nbytes) noexcept {
		assert(filter != nullptr);

		base.DisposeConsumed(nbytes);
	}

	bool InternalRead(bool expect_more) noexcept {
		assert(filter != nullptr);

#ifndef NDEBUG
		if (!base.IsConnected() && base.GetAvailable() == 0)
			/* work around assertion failure in
			   BufferedSocket::TryRead2() */
			return false;
#endif

		return base.Read(expect_more);
	}

	ssize_t InternalDirectWrite(const void *data, size_t length) noexcept {
		assert(filter != nullptr);

		return base.DirectWrite(data, length);
	}

	ssize_t InternalWrite(const void *data, size_t length) noexcept {
		assert(filter != nullptr);

		return base.Write(data, length);
	}

	/**
	 * A #SocketFilter must call this function whenever it adds data to
	 * its output buffer (only if it implements such a buffer).
	 */
	void InternalUndrained() noexcept {
		assert(filter != nullptr);
		assert(IsConnected());

		drained = false;
	}

	/**
	 * A #SocketFilter must call this function whenever its output buffer
	 * drains (only if it implements such a buffer).
	 */
	bool InternalDrained() noexcept;

	void InternalScheduleRead(bool expect_more,
				  Event::Duration timeout) noexcept {
		assert(filter != nullptr);

		base.ScheduleReadTimeout(expect_more, timeout);
	}

	void InternalScheduleWrite() noexcept {
		assert(filter != nullptr);

		base.ScheduleWrite();
	}

	void InternalUnscheduleWrite() noexcept {
		assert(filter != nullptr);

		base.UnscheduleWrite();
	}

	BufferedResult InvokeData() noexcept {
		assert(filter != nullptr);

		try {
			return handler->OnBufferedData();
		} catch (...) {
			handler->OnBufferedError(std::current_exception());
			return BufferedResult::CLOSED;
		}
	}

	bool InvokeClosed() noexcept {
		assert(filter != nullptr);

		return handler->OnBufferedClosed();
	}

	bool InvokeRemaining(size_t remaining) noexcept {
		assert(filter != nullptr);

		return handler->OnBufferedRemaining(remaining);
	}

	void InvokeEnd() noexcept {
		assert(filter != nullptr);
		assert(!ended);
		assert(base.HasEnded());

#ifndef NDEBUG
		ended = true;
#endif

		handler->OnBufferedEnd();
	}

	bool InvokeWrite() noexcept {
		assert(filter != nullptr);

		try {
			return handler->OnBufferedWrite();
		} catch (...) {
			handler->OnBufferedError(std::current_exception());
			return false;
		}
	}

	bool InvokeTimeout() noexcept;

	void InvokeError(std::exception_ptr e) noexcept {
		assert(filter != nullptr);

		handler->OnBufferedError(std::move(e));
	}

private:
	/* virtual methods from class BufferedSocketHandler */
	BufferedResult OnBufferedData() override;
	bool OnBufferedClosed() noexcept override;
	bool OnBufferedRemaining(size_t remaining) noexcept override;
	bool OnBufferedEnd() noexcept override;
	bool OnBufferedWrite() override;
	bool OnBufferedTimeout() noexcept override;
	enum write_result OnBufferedBroken() noexcept override;
	void OnBufferedError(std::exception_ptr e) noexcept override;
};
