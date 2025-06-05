// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <sys/types.h>

struct pool;
class UnusedIstreamPtr;
class FileDescriptor;
class SharedLease;
namespace Uring { class Queue; }

UnusedIstreamPtr
NewUringIstream(Uring::Queue &uring, struct pool &pool,
		const char *path, FileDescriptor fd, SharedLease &&lease,
		off_t start_offset, off_t end_offset) noexcept;
