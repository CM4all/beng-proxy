// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Sink.hxx"

/**
 * An #IstreamSink that blocks everything.
 */
class BlockSink final : public IstreamSink {
public:
	BlockSink(UnusedIstreamPtr &&_input) noexcept
		:IstreamSink(std::move(_input)) {}

	~BlockSink() noexcept {
		if (HasInput())
			CloseInput();
	}

	/* virtual methods from class IstreamHandler */

	std::size_t OnData(std::span<const std::byte>) noexcept override {
		return 0;
	}

	void OnEof() noexcept override {
		ClearInput();
	}

	void OnError(std::exception_ptr) noexcept override {
		ClearInput();
	}
};
