// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "istream/Sink.hxx"

#include <utility>

class StdioSink final : public IstreamSink {
public:
	template<typename I>
	explicit StdioSink(I &&_input) noexcept
		:IstreamSink(std::forward<I>(_input)) {}

	void LoopRead() noexcept {
		while (input.IsDefined())
			input.Read();
	}

	/* virtual methods from class IstreamHandler */
	size_t OnData(std::span<const std::byte> src) noexcept override;
	void OnEof() noexcept override;
	void OnError(std::exception_ptr ep) noexcept override;
};
