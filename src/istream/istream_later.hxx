// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

struct pool;
class UnusedIstreamPtr;
class EventLoop;

/**
 * An istream filter which delays the read() and eof() invocations.
 * This is used in the test suite.
 */
UnusedIstreamPtr
istream_later_new(struct pool &pool, UnusedIstreamPtr input,
		  EventLoop &event_loop) noexcept;
