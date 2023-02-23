// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "was/async/Socket.hxx"
#include "spawn/ProcessHandle.hxx"

#include <memory>
#include <span>

class SpawnService;
class ChildProcessHandle;
struct ChildOptions;

struct WasProcess : WasSocket {
	std::unique_ptr<ChildProcessHandle> handle;

	WasProcess() = default;

	explicit WasProcess(WasSocket &&_socket) noexcept
		:WasSocket(std::move(_socket)) {}
};

/**
 * Launch WAS child processes.
 *
 * Throws std::runtime_error on error.
 */
WasProcess
was_launch(SpawnService &spawn_service,
	   const char *name,
	   const char *executable_path,
	   std::span<const char *const> args,
	   const ChildOptions &options,
	   UniqueFileDescriptor stderr_fd);
