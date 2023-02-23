// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "event/Chrono.hxx"

struct pool;
class UnusedIstreamPtr;
class EventLoop;

/**
 * An istream that times out when no data has been received after a
 * certain amount of time.  The timer starts on the first
 * Istream::Read() call.
 */
UnusedIstreamPtr
NewTimeoutIstream(struct pool &pool, UnusedIstreamPtr input,
		  EventLoop &event_loop,
		  Event::Duration timeout) noexcept;
