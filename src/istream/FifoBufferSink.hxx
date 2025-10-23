// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "Sink.hxx"
#include "memory/SliceFifoBuffer.hxx"

class FifoBufferSinkHandler {
public:
	virtual bool OnFifoBufferSinkData() noexcept = 0;
	virtual void OnFifoBufferSinkEof() noexcept = 0;
	virtual void OnFifoBufferSinkError(std::exception_ptr ep) noexcept = 0;
};

/**
 * An #IstreamSink implementation which copies data into a FIFO buffer.
 */
class FifoBufferSink final : public IstreamSink {
	SliceFifoBuffer buffer;

	FifoBufferSinkHandler &handler;

public:
	template<typename I>
	FifoBufferSink(I &&_input, FifoBufferSinkHandler &_handler) noexcept
		:IstreamSink(std::forward<I>(_input)),
		 handler(_handler)
	{
		input.SetDirect(FD_ANY);
	}

	auto &GetBuffer() noexcept {
		return buffer;
	}

	void Read() noexcept {
		input.Read();
	}

protected:
	/* virtual methods from class IstreamHandler */
	IstreamReadyResult OnIstreamReady() noexcept override;
	std::size_t OnData(std::span<const std::byte> src) noexcept override;
	IstreamDirectResult OnDirect(FdType type, FileDescriptor fd,
				     off_t offset, std::size_t max_length,
				     bool then_eof) noexcept override;
	void OnEof() noexcept override;
	void OnError(std::exception_ptr &&ep) noexcept override;
};
