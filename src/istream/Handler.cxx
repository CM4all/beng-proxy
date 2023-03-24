// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Handler.hxx"
#include "io/FileDescriptor.hxx"
#include "util/Compiler.h"

[[gnu::noreturn]]
IstreamDirectResult
IstreamHandler::OnDirect([[maybe_unused]] FdType type,
			 [[maybe_unused]] FileDescriptor fd,
			 [[maybe_unused]] off_t offset,
			 [[maybe_unused]] std::size_t max_length,
			 [[maybe_unused]] bool then_eof) noexcept
{
	gcc_unreachable();
}
