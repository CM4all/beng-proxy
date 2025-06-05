// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "event/Chrono.hxx"
#include "util/BindMethod.hxx"

#include <span>

#include <sys/types.h>
#include <stddef.h>

enum class BufferedResult;
enum class BufferedReadResult;
class FilteredSocket;

class SocketFilter {
public:
	virtual void Init(FilteredSocket &_socket) noexcept = 0;

	/**
	 * @see FilteredSocket::SetHandshakeCallback()
	 */
	virtual void SetHandshakeCallback(BoundMethod<void() noexcept> callback) noexcept {
		callback();
	}

	/**
	 * Data has been read from the socket into the input buffer.  Call
	 * FilteredSocket::InternalReadBuffer() and
	 * FilteredSocket::InternalConsumed() to process data from the
	 * buffer.
	 */
	[[nodiscard]]
	virtual BufferedResult OnData() noexcept = 0;

	[[nodiscard]] [[gnu::pure]]
	virtual bool IsEmpty() const noexcept = 0;

	[[nodiscard]] [[gnu::pure]]
	virtual bool IsFull() const noexcept = 0;

	[[nodiscard]] [[gnu::pure]]
	virtual std::size_t GetAvailable() const noexcept = 0;

	[[nodiscard]] [[gnu::pure]]
	virtual std::span<std::byte> ReadBuffer() noexcept = 0;

	virtual void Consumed(std::size_t nbytes) noexcept = 0;

	virtual void AfterConsumed() noexcept = 0;

	/**
	 * The client asks to read more data.  The filter shall call
	 * FilteredSocket::InvokeData() again.
	 */
	[[nodiscard]]
	virtual BufferedReadResult Read() noexcept = 0;

	/**
	 * The client asks to write data to the socket.  The filter
	 * processes it, and may then call
	 * FilteredSocket::InvokeWrite().
	 */
	[[nodiscard]]
	virtual ssize_t Write(std::span<const std::byte> src) noexcept = 0;

	/**
	 * The client is willing to read, but does not expect it yet.  The
	 * filter processes the call, and may then call
	 * FilteredSocket::InternalScheduleRead().
	 */
	virtual void ScheduleRead() noexcept = 0;

	/**
	 * The client wants to be called back as soon as writing becomes
	 * possible.  The filter processes the call, and may then call
	 * FilteredSocket::InternalScheduleWrite().
	 */
	virtual void ScheduleWrite() noexcept = 0;

	/**
	 * The client is not anymore interested in writing.  The filter
	 * processes the call, and may then call
	 * FilteredSocket::InternalUnscheduleWrite().
	 */
	virtual void UnscheduleWrite() noexcept = 0;

	/**
	 * The underlying socket is ready for writing.  The filter may try
	 * calling FilteredSocket::InternalWrite() again.
	 *
	 * This method must not destroy the socket.  If an error occurs,
	 * it shall return false.
	 */
	[[nodiscard]]
	virtual bool InternalWrite() noexcept = 0;

	/**
	 * Prepare for shutdown of the socket.  This may send data on
	 * the socket.  After returning, check
	 * FilteredSocket::IsDrained() and wait for the
	 * OnBufferedDrained() callback.
	 *
	 * This method cannot fail.
	 */
	virtual void Shutdown() noexcept {}

	/**
	 * Called after the socket has been closed/abandoned (either by
	 * the peer or locally).  The filter shall update its internal
	 * state, but not do any invasive actions.
	 */
	virtual void OnClosed() noexcept {}

	[[nodiscard]]
	virtual bool OnRemaining(std::size_t remaining) noexcept = 0;

	/**
	 * The buffered_socket has run empty after the socket has been
	 * closed.  The filter may call FilteredSocket::InvokeEnd() as
	 * soon as all its buffers have been consumed.
	 *
	 * Throws on error.
	 */
	virtual void OnEnd() = 0;

	virtual void Close() noexcept = 0;
};
