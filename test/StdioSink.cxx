// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "StdioSink.hxx"
#include "util/PrintException.hxx"

#include <stdio.h>
#include <unistd.h>

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

void
StdioSink::OnEof() noexcept
{
	ClearInput();
}

void
StdioSink::OnError(std::exception_ptr ep) noexcept
{
	ClearInput();

	PrintException(ep);
}
