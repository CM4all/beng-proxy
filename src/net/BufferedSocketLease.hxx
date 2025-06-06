// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "event/net/BufferedSocket.hxx"
#include "util/DestructObserver.hxx"
#include "lease.hxx"

#include <array>

#include <sys/uio.h> // for struct iovec

/**
 * Wrapper for a #BufferedSocket which may be released at some point.
 * After that, remaining data in the input buffer can still be read.
 *
 * This class acts a #BufferedSocketHandler proxy to filter result
 * codes, when the socket has been released in the middle of a handler
 * method.
 */
class BufferedSocketLease final : BufferedSocketHandler {
	/* needed for translating the BufferedSocket::Read() return
	   value */
	DestructAnchor destruct_anchor;

	BufferedSocket *socket;
	LeasePtr lease_ref;

	BufferedSocketHandler &handler;

	SliceFifoBuffer input;

	struct HandlerInfo {
#ifndef NDEBUG
		bool released = false;
#endif
		PutAction action;
	};

	HandlerInfo *handler_info = nullptr;

public:
	BufferedSocketLease(BufferedSocket &_socket, Lease &lease,
			    Event::Duration write_timeout,
			    BufferedSocketHandler &_handler) noexcept;

	~BufferedSocketLease() noexcept;

	[[gnu::pure]]
	bool IsConnected() const noexcept {
		return socket != nullptr && socket->IsConnected();
	}

	void Close() const noexcept {
		socket->Close();
	}

#ifndef NDEBUG
	[[gnu::pure]]
	bool HasEnded() const noexcept {
		assert(!IsReleased());

		return socket->HasEnded();
	}
#endif

	/**
	 * @param preserve preserve the contents of the input buffer for
	 * further consumption?
	 */
	void Release(bool preserve, PutAction action) noexcept;

	bool IsReleased() const noexcept {
		return socket == nullptr;
	}

	[[gnu::pure]]
	FdType GetType() const noexcept {
		assert(!IsReleased());

		return socket->GetType();
	}

	void SetDirect(bool _direct) noexcept {
		assert(!IsReleased());

		socket->SetDirect(_direct);
	}

	[[gnu::pure]]
	bool IsEmpty() const noexcept;

	[[gnu::pure]]
	std::size_t GetAvailable() const noexcept;

	std::span<std::byte> ReadBuffer() const noexcept;

	void DisposeConsumed(std::size_t nbytes) noexcept;
	void AfterConsumed() noexcept;

	BufferedReadResult Read() noexcept;

	void ScheduleRead() noexcept {
		socket->ScheduleRead();
	}

	ssize_t Write(std::span<const std::byte>  src) noexcept {
		assert(!IsReleased());

		return socket->Write(src);
	}

	void DeferWrite() noexcept {
		assert(!IsReleased());

		socket->DeferWrite();
	}

	void DeferNextWrite() noexcept {
		assert(!IsReleased());

		socket->DeferNextWrite();
	}

	void ScheduleWrite() noexcept {
		assert(!IsReleased());

		socket->ScheduleWrite();
	}

	void UnscheduleWrite() noexcept {
		assert(!IsReleased());

		socket->UnscheduleWrite();
	}

	ssize_t WriteV(std::span<const struct iovec> v) noexcept {
		assert(!IsReleased());

		return socket->WriteV(v);
	}

	ssize_t WriteFrom(FileDescriptor fd, FdType fd_type, off_t *offset,
			  std::size_t length) noexcept {
		assert(!IsReleased());

		return socket->WriteFrom(fd, fd_type, offset, length);
	}

private:
	/**
	 * Move data from the #BufferedSocket input buffers to our #input
	 * buffers.  This is done prior to releasing the #BufferedSocket
	 * to be able to continue reading pending input.
	 */
	void MoveSocketInput() noexcept;

	bool IsReleasedEmpty() const noexcept {
		return input.empty();
	}

	bool ReadReleased() noexcept;

	/* virtual methods from class BufferedSocketHandler */
	BufferedResult OnBufferedData() override;
	DirectResult OnBufferedDirect(SocketDescriptor fd,
				      FdType fd_type) override;
	bool OnBufferedHangup() noexcept override;
	bool OnBufferedClosed() noexcept override;
	bool OnBufferedRemaining(std::size_t remaining) noexcept override;
	bool OnBufferedEnd() override;
	bool OnBufferedWrite() override;
	bool OnBufferedDrained() noexcept override;
	bool OnBufferedTimeout() noexcept override;
	enum write_result OnBufferedBroken() noexcept override;
	void OnBufferedError(std::exception_ptr e) noexcept override;
};
