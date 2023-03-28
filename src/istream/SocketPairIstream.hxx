// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

struct pool;
class UnusedIstreamPtr;
class EventLoop;

/**
 * Convert any file descriptor to a socket.  This #Istream
 * implementation is only used for unit tests.
 */
UnusedIstreamPtr
NewSocketPairIstream(struct pool &pool, EventLoop &event_loop,
		     UnusedIstreamPtr input) noexcept;
