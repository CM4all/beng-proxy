// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "SocketFilter.hxx"
#include "Ptr.hxx"
#include "event/net/BufferedSocket.hxx"
#include "util/BindMethod.hxx"

#include <cstddef>
#include <span>

class UniqueSocketDescriptor;

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

	/**
	 * Wrapper for InitDummy().
	 */
	FilteredSocket(EventLoop &_event_loop,
		       UniqueSocketDescriptor _fd, FdType _fd_type,
		       SocketFilterPtr _filter={});

	~FilteredSocket() noexcept;

	EventLoop &GetEventLoop() noexcept {
		return base.GetEventLoop();
	}

	void Init(SocketDescriptor fd, FdType fd_type,
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

	void Reinit(Event::Duration write_timeout,
		    BufferedSocketHandler &handler) noexcept;

	bool HasFilter() const noexcept {
		return filter != nullptr;
	}

	const SocketFilter *GetFilter() const noexcept {
		return filter.get();
	}

	/**
	 * Returns the underlying socket.  It may only be used to
	 * obtain metadata (socket options, addresses).  Don't do
	 * anything else with it.
	 */
	SocketDescriptor GetSocket() const noexcept {
		return base.GetSocket();
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
	 * do the latter, call Destroy().
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

	auto ClosedByPeer() noexcept {
		return base.ClosedByPeer();
	}

#ifndef NDEBUG
	bool HasEnded() const noexcept {
		return ended;
	}
#endif

	/**
	 * Destroy the object.  Prior to that, the socket must be
	 * removed by calling either Close() or Abandon().
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
	[[gnu::pure]]
	bool IsEmpty() const noexcept;

	/**
	 * Is the input buffer full?
	 */
	[[gnu::pure]]
	bool IsFull() const noexcept;

	/**
	 * Returns the number of bytes in the input buffer.
	 */
	[[gnu::pure]]
	std::size_t GetAvailable() const noexcept;

	std::span<std::byte> ReadBuffer() const noexcept;

	/**
	 * Dispose the specified number of bytes from the input buffer.
	 * Call this after ReadBuffer().  It may be called repeatedly.
	 */
	void DisposeConsumed(std::size_t nbytes) noexcept;

	void AfterConsumed() noexcept;

	void SetDirect(bool _direct) noexcept {
		assert(!_direct || !HasFilter());

		base.SetDirect(_direct);
	}

	/**
	 * The caller wants to read more data from the socket.  There
	 * are four possible outcomes: a call to
	 * BufferedSocketHandler::OnBufferedData(), a call to
	 * BufferedSocketHandler::OnBufferedDirect(), a call to
	 * BufferedSocketHandler::OnBufferedError() or (if there is no
	 * data available yet) an event gets scheduled and the
	 * function returns immediately.
	 */
	BufferedReadResult Read() noexcept;

	ssize_t Write(std::span<const std::byte> src) noexcept;

	ssize_t WriteV(std::span<const struct iovec> v) noexcept {
		assert(filter == nullptr);

		return base.WriteV(v.data(), v.size());
	}

	ssize_t WriteFrom(FileDescriptor fd, FdType fd_type, off_t *offset,
			  std::size_t length) noexcept {
		assert(filter == nullptr);

		return base.WriteFrom(fd, fd_type, offset, length);
	}

	[[gnu::pure]]
	bool IsReadyForWriting() const noexcept {
		assert(filter == nullptr);

		return base.IsReadyForWriting();
	}

	/**
	 * Wrapper for BufferedSocket::DeferRead().  This works only
	 * for the initial read.
	 */
	void DeferRead() noexcept {
		/* this is only relevant if there is no filter; with a
		   filter, reading is always scheduled (unless the
		   buffer is full) */
		if (filter == nullptr)
			base.DeferRead();
	}

	void ScheduleRead() noexcept {
		if (filter != nullptr)
			filter->ScheduleRead();
		else
			base.ScheduleRead();
	}


	void DeferWrite() noexcept {
		if (filter != nullptr)
			filter->ScheduleWrite();
		else
			base.DeferWrite();
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

	[[gnu::pure]]
	bool InternalIsEmpty() const noexcept {
		assert(filter != nullptr);

		return base.IsEmpty();
	}

	[[gnu::pure]]
	bool InternalIsFull() const noexcept {
		assert(filter != nullptr);

		return base.IsFull();
	}

	[[gnu::pure]]
	std::size_t InternalGetAvailable() const noexcept {
		assert(filter != nullptr);

		return base.GetAvailable();
	}

	std::span<std::byte> InternalReadBuffer() const noexcept {
		assert(filter != nullptr);

		return base.ReadBuffer();
	}

	void InternalConsumed(std::size_t nbytes) noexcept {
		assert(filter != nullptr);

		base.DisposeConsumed(nbytes);
	}

	void InternalAfterConsumed() noexcept {
		assert(filter != nullptr);

		base.AfterConsumed();
	}

	DefaultFifoBuffer &InternalGetInputBuffer() noexcept {
		return base.GetInputBuffer();
	}

	const DefaultFifoBuffer &GetInputBuffer() const noexcept {
		return base.GetInputBuffer();
	}

	BufferedReadResult InternalRead() noexcept {
		assert(filter != nullptr);

		return base.Read();
	}

	ssize_t InternalDirectWrite(std::span<const std::byte> src) noexcept {
		assert(filter != nullptr);

		return base.DirectWrite(src.data(), src.size());
	}

	ssize_t InternalWrite(std::span<const std::byte> src) noexcept {
		assert(filter != nullptr);

		return base.Write(src.data(), src.size());
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

	void InternalScheduleRead() noexcept {
		assert(filter != nullptr);

		base.ScheduleRead();
	}

	void InternalScheduleWrite() noexcept {
		assert(filter != nullptr);

		base.ScheduleWrite();
	}

	void InternalDeferWrite() noexcept {
		assert(filter != nullptr);

		base.DeferWrite();
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
			return BufferedResult::DESTROYED;
		}
	}

	bool InvokeClosed() noexcept {
		assert(filter != nullptr);

		return handler->OnBufferedClosed();
	}

	bool InvokeRemaining(std::size_t remaining) noexcept {
		assert(filter != nullptr);

		return handler->OnBufferedRemaining(remaining);
	}

	void InvokeEnd() {
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
	bool OnBufferedHangup() noexcept override;
	bool OnBufferedClosed() noexcept override;
	bool OnBufferedRemaining(std::size_t remaining) noexcept override;
	bool OnBufferedEnd() override;
	bool OnBufferedWrite() override;
	bool OnBufferedTimeout() noexcept override;
	enum write_result OnBufferedBroken() noexcept override;
	void OnBufferedError(std::exception_ptr e) noexcept override;
};
