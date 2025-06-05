// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

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

	enum class DechunkInputAction {
		/**
		 * Keep the #DechunkIstream open but abandon the
		 * pointer to it.
		 */
		ABANDON,

		/**
		 * Close the #DechunkIstream.
		 */
		CLOSE,

		/**
		 * The method has destroyed the #DechunkIstream.
		 */
		DESTROYED,
	};

	/**
	 * Called after the end chunk has been consumed from the input,
	 * right before calling IstreamHandler::OnEof().
	 */
	virtual DechunkInputAction OnDechunkEnd() noexcept = 0;
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
