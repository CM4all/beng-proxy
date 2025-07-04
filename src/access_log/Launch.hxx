// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

/*
 * Launch logger child processes.
 */

#pragma once

#include "net/UniqueSocketDescriptor.hxx"

#include <span>

#include <sys/types.h>

struct UidGid;

struct LogProcess {
	pid_t pid;
	UniqueSocketDescriptor fd;
};

LogProcess
LaunchLogger(const char *command,
	     const UidGid *uid_gid);

UniqueSocketDescriptor
LaunchLogger(std::span<const char *const> args);
