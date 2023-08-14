// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

struct pool;
class EventLoop;
class UnusedIstreamPtr;

class DechunkHandler {
public:
	/**
	 * Called as soon as the dechunker has seen the end chunk in data
	 * provided by the input.  At this time, the end chunk may not yet
	 * ready to be processed, but it's an indicator that input's
	 * underlying socket is done.
	 */
	virtual void OnDechunkEndSeen() noexcept = 0;

	/**
	 * Called after the end chunk has been consumed from the input,
	 * right before calling IstreamHandler::OnEof().
	 *
	 * @return false if the caller shall close its input
	 */
	virtual bool OnDechunkEnd() noexcept = 0;
};

/**
 * This istream filter removes HTTP chunking.
 *
 * @param eof_callback a callback function which is called when the
 * last chunk is being consumed; note that this occurs inside the
 * data() callback, so the istream doesn't know yet how much is
 * consumed
 */
UnusedIstreamPtr
istream_dechunk_new(struct pool &pool, UnusedIstreamPtr input,
		    EventLoop &event_loop,
		    DechunkHandler &dechunk_handler) noexcept;
