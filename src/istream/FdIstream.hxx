// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "io/FdType.hxx"

struct pool;
class UnusedIstreamPtr;
class EventLoop;
class UniqueFileDescriptor;

UnusedIstreamPtr
NewFdIstream(EventLoop &event_loop, struct pool &pool,
	     const char *path,
	     UniqueFileDescriptor fd, FdType fd_type) noexcept;
