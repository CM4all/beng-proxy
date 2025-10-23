// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "CountIstreamSink.hxx"
#include "io/FileDescriptor.hxx"

#include <array>
#include <cassert>

std::size_t
CountIstreamSink::OnData(std::span<const std::byte> src) noexcept
{
	count += src.size();
	return src.size();
}

IstreamDirectResult
CountIstreamSink::OnDirect([[maybe_unused]] FdType type, FileDescriptor fd,
			   off_t offset,
			   std::size_t max_length,
			   bool then_eof) noexcept
{
	assert(fd.IsDefined());
	assert(max_length > 0);

	IstreamDirectResult result = IstreamDirectResult::END;

	do {
		std::array<std::byte, 16384> buffer;
		std::span<std::byte> w{buffer};
		if (w.size() > max_length)
			w = w.first(max_length);

		const ssize_t nbytes = offset >= 0
			? fd.ReadAt(offset, w)
			: fd.Read(w);
		if (nbytes < 0)
			return IstreamDirectResult::ERRNO;
		if (nbytes == 0)
			break;

		std::size_t n = static_cast<std::size_t>(nbytes);
		count += n;
		input.ConsumeDirect(n);
		result = IstreamDirectResult::OK;

		if (n == max_length && then_eof) {
			CloseInput();
			return IstreamDirectResult::CLOSED;
		}

		max_length -= n;

		if (n < w.size())
			break;
	} while (max_length > 0);

	return result;
}

void
CountIstreamSink::OnEof() noexcept
{
	ClearInput();
}

void
CountIstreamSink::OnError(std::exception_ptr &&_error) noexcept
{
	ClearInput();
	error = std::move(_error);
}
