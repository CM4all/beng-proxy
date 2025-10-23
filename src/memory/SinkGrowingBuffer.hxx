// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "GrowingBuffer.hxx"
#include "istream/Sink.hxx"

class GrowingBufferSinkHandler {
public:
	virtual void OnGrowingBufferSinkEof(GrowingBuffer buffer) noexcept = 0;
	virtual void OnGrowingBufferSinkError(std::exception_ptr error) noexcept = 0;
};

/**
 * An #IstreamSink implementation which copies data into a #GrowingBuffer.
 */
class GrowingBufferSink final : public IstreamSink {
	GrowingBuffer buffer;

	GrowingBufferSinkHandler &handler;

public:
	template<typename I>
	GrowingBufferSink(I &&_input, GrowingBufferSinkHandler &_handler) noexcept
		:IstreamSink(std::forward<I>(_input)),
		 handler(_handler)
	{
		input.SetDirect(FD_ANY);
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
	void OnError(std::exception_ptr &&error) noexcept override;
};
