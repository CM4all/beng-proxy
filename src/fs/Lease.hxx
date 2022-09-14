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

#pragma once

#include "FilteredSocket.hxx"
#include "lease.hxx"

#include <array>

#include <sys/uio.h> // for struct iovec

/**
 * Wrapper for a #FilteredSocket which may be released at some point.
 * After that, remaining data in the input buffer can still be read.
 *
 * This class acts a #BufferedSocketHandler proxy to filter result
 * codes, when the socket has been released in the middle of a handler
 * method.
 */
class FilteredSocketLease final : BufferedSocketHandler {
	FilteredSocket *socket;
	LeasePtr lease_ref;

	BufferedSocketHandler &handler;

	std::array<SliceFifoBuffer, 4> input;

public:
	FilteredSocketLease(FilteredSocket &_socket, Lease &lease,
			    Event::Duration write_timeout,
			    BufferedSocketHandler &_handler) noexcept;

	~FilteredSocketLease() noexcept;

	[[gnu::pure]]
	bool IsConnected() const noexcept {
		return socket != nullptr && socket->IsConnected();
	}

	void Close() const noexcept {
		socket->Close();
	}

	[[gnu::pure]]
	bool HasFilter() const noexcept {
		assert(!IsReleased());

		return socket->HasFilter();
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
	void Release(bool preserve, bool reuse) noexcept;

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

	int AsFD() noexcept {
		assert(!IsReleased());

		return socket->AsFD();
	}

	[[gnu::pure]]
	bool IsEmpty() const noexcept;

	[[gnu::pure]]
	size_t GetAvailable() const noexcept;

	std::span<std::byte> ReadBuffer() const noexcept;

	void DisposeConsumed(size_t nbytes) noexcept;
	void AfterConsumed() noexcept;

	bool Read(bool expect_more) noexcept;

	void ScheduleRead(bool expect_more) noexcept {
		socket->ScheduleRead(expect_more);
	}

	ssize_t Write(std::span<const std::byte>  src) noexcept {
		assert(!IsReleased());

		return socket->Write(src);
	}

	void DeferWrite() noexcept {
		assert(!IsReleased());

		socket->DeferWrite();
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
	 * Move data from the #FilteredSocket input buffers to our #input
	 * buffers.  This is done prior to releasing the #FilteredSocket
	 * to be able to continue reading pending input.
	 */
	void MoveSocketInput() noexcept;

	/**
	 * Move data to the front-most #input buffer.
	 */
	void MoveInput() noexcept;

	bool IsReleasedEmpty() const noexcept {
		return input.front().empty();
	}

	bool ReadReleased() noexcept;

	/* virtual methods from class BufferedSocketHandler */
	BufferedResult OnBufferedData() override;
	DirectResult OnBufferedDirect(SocketDescriptor fd,
				      FdType fd_type) override;
	bool OnBufferedHangup() noexcept override;
	bool OnBufferedClosed() noexcept override;
	bool OnBufferedRemaining(size_t remaining) noexcept override;
	bool OnBufferedEnd() noexcept override;
	bool OnBufferedWrite() override;
	bool OnBufferedDrained() noexcept override;
	bool OnBufferedTimeout() noexcept override;
	enum write_result OnBufferedBroken() noexcept override;
	void OnBufferedError(std::exception_ptr e) noexcept override;
};
