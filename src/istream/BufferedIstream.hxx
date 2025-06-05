// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <exception>

struct pool;
class EventLoop;
class PipeStock;
class CancellablePointer;
class UnusedIstreamPtr;

/**
 * Handler interface for NewBufferedIstream().
 */
class BufferedIstreamHandler {
public:
	virtual void OnBufferedIstreamReady(UnusedIstreamPtr i) noexcept = 0;
	virtual void OnBufferedIstreamError(std::exception_ptr e) noexcept = 0;
};

/**
 * This asynchronous class registers itself as #IstreamHandler for the
 * given #Istream and waits until data becomes available.  As soon as
 * data arrives, it is collected in a pipe or in a buffer.  When the
 * buffer is full, it invokes the #BufferedIstreamHandler and gives it
 * a new #Istream with buffered data plus remaining data.
 *
 * This class can be useful to postpone invoking filter processes
 * until there is really data, to avoid blocking filter processes
 * while there is nothing to do yet.
 */
void
NewBufferedIstream(struct pool &pool, EventLoop &event_loop,
		   PipeStock *pipe_stock,
		   BufferedIstreamHandler &handler,
		   UnusedIstreamPtr i,
		   CancellablePointer &cancel_ptr) noexcept;
