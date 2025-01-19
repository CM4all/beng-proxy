// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <exception>

#include <stdint.h>

struct pool;
class EventLoop;
class FileDescriptor;
class UnusedIstreamPtr;
class WasOutput;

class WasOutputHandler {
public:
	/**
	 * Announces the length of the resource.
	 *
	 * @param true on success, false if the #WasOutput object has been
	 * deleted
	 */
	virtual bool WasOutputLength(uint64_t length) noexcept = 0;

	/**
	 * The stream ended prematurely, but the #WasOutput object is
	 * still ok.
	 *
	 * @param the number of bytes aready sent
	 * @param true on success, false if the #WasOutput object has been
	 * deleted
	 */
	virtual bool WasOutputPremature(uint64_t length,
					std::exception_ptr ep) noexcept = 0;

	virtual void WasOutputEof() noexcept = 0;

	virtual void WasOutputError(std::exception_ptr ep) noexcept = 0;
};

/**
 * Web Application Socket protocol, output data channel library.
 */
WasOutput *
was_output_new(struct pool &pool, EventLoop &event_loop,
	       FileDescriptor fd, UnusedIstreamPtr input,
	       WasOutputHandler &handler) noexcept;

/**
 * @return the total number of bytes written to the pipe
 */
uint64_t
was_output_free(WasOutput *data) noexcept;

/**
 * Check if we can provide the LENGTH header.
 *
 * @return the WasOutputHandler::length() return value
 */
bool
was_output_check_length(WasOutput &output) noexcept;
