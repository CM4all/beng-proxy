// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <sys/types.h>

struct pool;
class UnusedIstreamPtr;
class UniqueFileDescriptor;
namespace Uring { class Queue; }

UnusedIstreamPtr
NewUringIstream(Uring::Queue &uring, struct pool &pool,
		const char *path, UniqueFileDescriptor fd,
		off_t start_offset, off_t end_offset) noexcept;
