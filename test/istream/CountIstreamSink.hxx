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

	void EnableDirect() noexcept {
		input.SetDirect(FD_ANY);
	}

	bool IsDone() const noexcept {
		return !HasInput();
	}

	void RethrowError() const {
		if (error)
			std::rethrow_exception(error);
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
	IstreamDirectResult OnDirect(FdType type, FileDescriptor fd,
				     off_t offset,
				     std::size_t max_length,
				     bool then_eof) noexcept override;
	void OnEof() noexcept override;
	void OnError(std::exception_ptr &&_error) noexcept override;
};
