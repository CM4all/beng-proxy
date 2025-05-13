// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "memory/SliceFifoBuffer.hxx"

#include <memory>
#include <mutex>

struct pool;
class UnusedIstreamPtr;
class ThreadQueue;

/**
 * Gives #ThreadIstreamFilter access to some of the internals of
 * #ThreadIstream.  This is used to transfer data and state between
 * the two.
 */
struct ThreadIstreamInternal {
	std::mutex mutex;

	/**
	 * Incoming data, not yet filtered.  Filled by the
	 * #ThreadIstream.
	 *
	 * Protected by #mutex.
	 */
	SliceFifoBuffer input;

	/**
	 * Outgoing data, already filtered.  Allocated by the
	 * #ThreadIstream.
	 *
	 * Protected by #mutex.
	 */
	SliceFifoBuffer output;

	/**
	 * True when #ThreadIstreamFilter's internal output buffers
	 * are empty.  Set by #ThreadIstreamFilter::Run() before
	 * returning.
	 *
	 * Protected by #mutex.
	 */
	bool drained = true;

	/**
	 * False when no more input will ever be provided in this
	 * instance.  At this point, data in #input is complete.
	 *
	 * Protected by #mutex.
	 */
	bool has_input = true;

	/**
	 * Schedule the job again?  This can be used to fix up things
	 * that can only be done in the main thread (e.g. allocate
	 * more buffers from the #SlicePool).
	 *
	 * Protected by #mutex.
	 */
	bool again = false;

protected:
	ThreadIstreamInternal() noexcept = default;
	~ThreadIstreamInternal() noexcept;

	ThreadIstreamInternal(const ThreadIstreamInternal &) = delete;
	ThreadIstreamInternal &operator=(const ThreadIstreamInternal &) = delete;
};

class ThreadIstreamFilter {
public:
	virtual ~ThreadIstreamFilter() noexcept = default;

	/**
	 * Called in the main thread before Run() is scheduled.  This
	 * can be used to prepare things that can only be done in the
	 * main thread, e.g. allocate more (internal) buffers.
	 *
	 * @return true if Run() shall be invoked, false if conditions
	 * for Run() are not met
	 */
	[[nodiscard]]
	virtual bool PreRun(ThreadIstreamInternal &) noexcept {
		return true;
	}

	/**
	 * Do the work.  This is run in an unspecified worker thread.
	 * The given #ThreadIstreamInternal's mutex may be used for
	 * protection.
	 *
	 * This method may throw exceptions, which will be forwarded
	 * to IstreamHandler::OnError().
	 */
	virtual void Run(ThreadIstreamInternal &i) = 0;

	/**
	 * Called in the main thread after one or more Run() calls
	 * have finished successfully.
	 */
	virtual void PostRun(ThreadIstreamInternal &) noexcept {}

	/**
	 * Called in the main thread while the worker thread runs
	 * Run() and is unable to cancel it; this gives the
	 * #ThreadIstreamFilter a chance to fast-track cancellation.
	 *
	 * This cancellation may be permanent; it is only used while
	 * shutting down the connection.
	 */
	virtual void CancelRun(ThreadIstreamInternal &) noexcept {}
};

/**
 * Creates a new #Istream with a #ThreadIstreamFilter that filters all
 * data, where calls to ThreadIstreamFilter::Run() calls are offloaded
 * to a worker thread.
 */
UnusedIstreamPtr
NewThreadIstream(struct pool &pool, ThreadQueue &queue,
		 UnusedIstreamPtr input,
		 std::unique_ptr<ThreadIstreamFilter> filter) noexcept;
