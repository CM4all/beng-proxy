/*
 * Copyright 2007-2020 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "io/FdType.hxx"

#include <exception>

struct pool;
class EventLoop;
class FileDescriptor;
class UnusedIstreamPtr;
struct SinkFd;

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
