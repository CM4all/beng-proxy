// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

struct pool;
class UnusedIstreamPtr;
class EventLoop;

/**
 * An #Istream implementation which copies its input to one or more
 * outputs.
 *
 * Data gets delivered to the first output, then to the second output
 * and so on.  Destruction (eof / abort) goes the reverse order: the
 * last output gets destructed first.
 *
 * @param input the istream which is duplicated
 * @param weak if true, closes the whole object if only this output
 * (and possibly other "weak" outputs) remains
 * @param defer_read schedule a deferred Istream::Read() call
 */
UnusedIstreamPtr
NewTeeIstream(struct pool &pool, UnusedIstreamPtr input,
	      EventLoop &event_loop,
	      bool weak,
	      bool defer_read=false) noexcept;

/**
 * Create another output for the given #TeeIstream.
 */
UnusedIstreamPtr
AddTeeIstream(UnusedIstreamPtr &tee, bool weak) noexcept;
