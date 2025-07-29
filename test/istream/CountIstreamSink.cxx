// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "CountIstreamSink.hxx"

std::size_t
CountIstreamSink::OnData(std::span<const std::byte> src) noexcept
{
	count += src.size();
	return src.size();
}

void
CountIstreamSink::OnEof() noexcept
{
	ClearInput();
}

void
CountIstreamSink::OnError(std::exception_ptr _error) noexcept
{
	ClearInput();
	error = std::move(_error);
}
