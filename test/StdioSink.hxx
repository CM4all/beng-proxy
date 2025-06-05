// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "istream/Sink.hxx"
#include "util/PrintException.hxx"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

struct StdioSink final : IstreamSink {
	template<typename I>
	explicit StdioSink(I &&_input)
		:IstreamSink(std::forward<I>(_input)) {}

	void LoopRead() {
		while (input.IsDefined())
			input.Read();
	}

	/* virtual methods from class IstreamHandler */

	size_t OnData(std::span<const std::byte> src) noexcept override;

	void OnEof() noexcept override {
		ClearInput();
	}

	void OnError(std::exception_ptr ep) noexcept override {
		ClearInput();

		PrintException(ep);
	}
};

size_t
StdioSink::OnData(std::span<const std::byte> src) noexcept
{
	ssize_t nbytes = write(STDOUT_FILENO, src.data(), src.size());
	if (nbytes < 0) {
		perror("failed to write to stdout");
		CloseInput();
		return 0;
	}

	if (nbytes == 0) {
		fprintf(stderr, "failed to write to stdout\n");
		CloseInput();
		return 0;
	}

	return (size_t)nbytes;
}
