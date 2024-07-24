// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

struct pool;
class UnusedIstreamPtr;
class EventLoop;

/**
 * An #Istream filter which attempts to translate OnData() calls to
 * OnIstreamReady().
 *
 * Only for unit tests.
 */
UnusedIstreamPtr
NewReadyIstream(EventLoop &event_loop,
		struct pool &pool, UnusedIstreamPtr input) noexcept;
