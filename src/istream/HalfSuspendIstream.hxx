// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "event/Chrono.hxx"

struct pool;
class UnusedIstreamPtr;
class EventLoop;

/**
 * An istream filter which suspends the second half of the transfer
 * for a certain duration.
 */
UnusedIstreamPtr
NewHalfSuspendIstream(struct pool &pool, UnusedIstreamPtr input,
		      EventLoop &event_loop, Event::Duration delay) noexcept;
