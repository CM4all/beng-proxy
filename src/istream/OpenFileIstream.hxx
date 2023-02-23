// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

struct pool;
class UnusedIstreamPtr;
class EventLoop;

/**
 * Throws exception on error.
 */
UnusedIstreamPtr
OpenFileIstream(EventLoop &event_loop, struct pool &pool, const char *path);
