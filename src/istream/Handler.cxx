// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Handler.hxx"
#include "io/FileDescriptor.hxx"

#include <utility> // for std::unreachable()

[[gnu::noreturn]]
IstreamDirectResult
IstreamHandler::OnDirect([[maybe_unused]] FdType type,
			 [[maybe_unused]] FileDescriptor fd,
			 [[maybe_unused]] off_t offset,
			 [[maybe_unused]] std::size_t max_length,
			 [[maybe_unused]] bool then_eof) noexcept
{
	std::unreachable();
}
