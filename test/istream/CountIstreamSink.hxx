// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "istream/Sink.hxx"
#include "istream/UnusedPtr.hxx"

/**
 * An #IstreamSink that counts the number of bytes.
 */
class CountIstreamSink final : IstreamSink {
	std::exception_ptr error;
	std::size_t count = 0;

public:
	explicit CountIstreamSink(UnusedIstreamPtr &&_input) noexcept
		:IstreamSink(std::move(_input)) {}

	bool IsDone() const noexcept {
		return !HasInput();
	}

	void Read() noexcept {
		input.Read();
	}

	std::size_t GetCount() const noexcept {
		return count;
	}

protected:
	/* virtual methods from class IstreamHandler */
	std::size_t OnData(std::span<const std::byte> src) noexcept override;
	void OnEof() noexcept override;
	void OnError(std::exception_ptr _error) noexcept override;
};
