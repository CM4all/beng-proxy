// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "io/FdType.hxx"

#include <exception>

struct pool;
class EventLoop;
class FileDescriptor;
class UnusedIstreamPtr;
class SinkFd;

class SinkFdHandler {
public:
	/**
	 * Called when end-of-file has been received from the istream.
	 */
	virtual void OnInputEof() noexcept = 0;

	/**
	 * Called when an error has been reported by the istream, right
	 * before the sink is destructed.
	 */
	virtual void OnInputError(std::exception_ptr ep) noexcept = 0;

	/**
	 * Called when a send error has occurred on the socket, right
	 * before the sink is destructed.
	 *
	 * @return true to close the stream, false when this method has
	 * already destructed the sink
	 */
	virtual bool OnSendError(int error) noexcept = 0;
};

/**
 * An #IstreamHandler which sends data to a file descriptor.
 */
SinkFd *
sink_fd_new(EventLoop &event_loop, struct pool &pool, UnusedIstreamPtr istream,
	    FileDescriptor fd, FdType fd_type,
	    SinkFdHandler &handler) noexcept;

void
sink_fd_read(SinkFd *ss) noexcept;

void
sink_fd_close(SinkFd *ss) noexcept;
