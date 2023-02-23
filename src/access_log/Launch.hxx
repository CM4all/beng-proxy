// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * Launch logger child processes.
 */

#pragma once

#include "net/UniqueSocketDescriptor.hxx"

#include <sys/types.h>

struct UidGid;
template<typename T> struct ConstBuffer;

struct LogProcess {
	pid_t pid;
	UniqueSocketDescriptor fd;
};

LogProcess
LaunchLogger(const char *command,
	     const UidGid *uid_gid);

UniqueSocketDescriptor
LaunchLogger(ConstBuffer<const char *> args);
