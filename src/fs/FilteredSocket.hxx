// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "SocketFilter.hxx"
#include "Ptr.hxx"
#include "event/net/BufferedSocket.hxx"
#include "util/BindMethod.hxx"

#include <cstddef>
#include <span>

#ifdef _LIBCPP_VERSION
/* with libc++, we can't pass a span<iovec> when iovec is
   forward-declared*/
#include <sys/uio.h>
#endif

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

	bool shutting_down;

public:
	[[nodiscard]]
	explicit FilteredSocket(EventLoop &_event_loop) noexcept
		:base(_event_loop) {}

	/**
	 * Wrapper for InitDummy().
	 */
	[[nodiscard]]
	FilteredSocket(EventLoop &_event_loop,
		       UniqueSocketDescriptor _fd, FdType _fd_type,
		       SocketFilterPtr _filter={});

	~FilteredSocket() noexcept;

	[[nodiscard]]
	EventLoop &GetEventLoop() noexcept {
		return base.GetEventLoop();
	}

#ifdef HAVE_URING
	void EnableUring(Uring::Queue &uring_queue) noexcept {
		base.EnableUring(uring_queue);
	}

	[[gnu::pure]]
	Uring::Queue *GetUringQueue() const noexcept {
		return base.GetUringQueue();
	}
#endif

	bool HasUring() const noexcept {
		return base.HasUring();
	}

	void Init(UniqueSocketDescriptor &&fd, FdType fd_type,
		  Event::Duration write_timeout,
		  SocketFilterPtr filter,
		  BufferedSocketHandler &handler) noexcept;

	/**
	 * Initialize a "dummy" instance (without a filter) which cannot
	 * be used to schedule events (because there is no handler); the
	 * next Reinit() call finishes initialization.
	 */
	void InitDummy(UniqueSocketDescriptor &&_fd, FdType _fd_type,
		       SocketFilterPtr _filter={}) noexcept;

	void Reinit(Event::Duration write_timeout,
		    BufferedSocketHandler &handler) noexcept;

	[[nodiscard]]
	bool HasFilter() const noexcept {
		return filter != nullptr;
	}

	[[nodiscard]]
	const SocketFilter *GetFilter() const noexcept {
		return filter.get();
	}

	/**
	 * Returns the underlying socket.  It may only be used to
	 * obtain metadata (socket options, addresses).  Don't do
	 * anything else with it.
	 */
	[[nodiscard]]
	SocketDescriptor GetSocket() const noexcept {
		return base.GetSocket();
	}

	[[nodiscard]]
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

	/**
	 * Prepare for shutdown of the socket.  This may send data on
	 * the socket.  After returning, check IsDrained() and wait
	 * for the OnBufferedDrained() callback.
	 *
	 * This method cannot fail.
	 */
	void Shutdown() noexcept {
		assert(!shutting_down);

		if (filter != nullptr) {
			filter->Shutdown();

			if (!IsDrained()) {
				shutting_down = true;
				return;
			}
		}

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

	[[nodiscard]]
	auto ClosedByPeer() noexcept {
		return base.ClosedByPeer();
	}

#ifndef NDEBUG
	[[nodiscard]]
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
	 * Is the socket still connected?  This does not actually check
	 * whether the socket is connected, just whether it is known to be
	 * closed.
	 */
	[[nodiscard]]
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
	[[nodiscard]]
	bool IsValid() const noexcept {
		return base.IsValid();
	}

	/**
	 * Accessor for #drained.
	 */
	[[nodiscard]]
	bool IsDrained() const noexcept {
		assert(IsValid());

		return drained;
	}

	/**
	 * Is the input buffer empty?
	 */
	[[nodiscard]] [[gnu::pure]]
	bool IsEmpty() const noexcept;

	/**
	 * Is the input buffer full?
	 */
	[[nodiscard]] [[gnu::pure]]
	bool IsFull() const noexcept;

	/**
	 * Returns the number of bytes in the input buffer.
	 */
	[[nodiscard]] [[gnu::pure]]
	std::size_t GetAvailable() const noexcept;

	[[nodiscard]]
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
	[[nodiscard]]
	BufferedReadResult Read() noexcept;

	[[nodiscard]]
	ssize_t Write(std::span<const std::byte> src) noexcept;

	[[nodiscard]]
	ssize_t WriteV(std::span<const struct iovec> v) noexcept {
		assert(filter == nullptr);

		return base.WriteV(v);
	}

	[[nodiscard]]
	ssize_t WriteFrom(FileDescriptor fd, FdType fd_type, off_t *offset,
			  std::size_t length) noexcept {
		assert(filter == nullptr);

		return base.WriteFrom(fd, fd_type, offset, length);
	}

	[[nodiscard]] [[gnu::pure]]
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

	[[nodiscard]] [[gnu::pure]]
	bool InternalIsEmpty() const noexcept {
		assert(filter != nullptr);

		return base.IsEmpty();
	}

	[[nodiscard]] [[gnu::pure]]
	bool InternalIsFull() const noexcept {
		assert(filter != nullptr);

		return base.IsFull();
	}

	[[nodiscard]] [[gnu::pure]]
	std::size_t InternalGetAvailable() const noexcept {
		assert(filter != nullptr);

		return base.GetAvailable();
	}

	[[nodiscard]]
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

	[[nodiscard]]
	DefaultFifoBuffer &InternalGetInputBuffer() noexcept {
		return base.GetInputBuffer();
	}

	[[nodiscard]]
	const DefaultFifoBuffer &GetInputBuffer() const noexcept {
		return base.GetInputBuffer();
	}

	[[nodiscard]]
	BufferedReadResult InternalRead() noexcept {
		assert(filter != nullptr);

		return base.Read();
	}

	[[nodiscard]]
	ssize_t InternalDirectWrite(std::span<const std::byte> src) noexcept {
		assert(filter != nullptr);

		return base.DirectWrite(src);
	}

	[[nodiscard]]
	ssize_t InternalWrite(std::span<const std::byte> src) noexcept {
		assert(filter != nullptr);

		return base.Write(src);
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
	[[nodiscard]]
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

	void InternalShutdown() noexcept {
		base.Shutdown();
	}

	[[nodiscard]]
	BufferedResult InvokeData() noexcept {
		assert(filter != nullptr);

		try {
			return handler->OnBufferedData();
		} catch (...) {
			handler->OnBufferedError(std::current_exception());
			return BufferedResult::DESTROYED;
		}
	}

	[[nodiscard]]
	bool InvokeClosed() noexcept {
		assert(filter != nullptr);

		return handler->OnBufferedClosed();
	}

	[[nodiscard]]
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

	[[nodiscard]]
	bool InvokeWrite() noexcept {
		assert(filter != nullptr);

		try {
			return handler->OnBufferedWrite();
		} catch (...) {
			handler->OnBufferedError(std::current_exception());
			return false;
		}
	}

	[[nodiscard]]
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
