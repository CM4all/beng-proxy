// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <exception>

#include <stdint.h>

struct pool;
class EventLoop;
class FileDescriptor;
class UnusedIstreamPtr;
class WasOutputSink;

/**
 * Handler for #WasOutputSink.
 */
class WasOutputSinkHandler {
public:
	/**
	 * Announces the length of the resource.
	 *
	 * @param true on success, false if the #WasOutputSink object has been
	 * deleted
	 */
	virtual bool WasOutputSinkLength(uint64_t length) noexcept = 0;

	/**
	 * The stream ended prematurely, but the #WasOutputSink object is
	 * still ok.
	 *
	 * @param the number of bytes aready sent
	 * @param true on success, false if the #WasOutputSink object has been
	 * deleted
	 */
	virtual bool WasOutputSinkPremature(uint64_t length,
					std::exception_ptr ep) noexcept = 0;

	virtual void WasOutputSinkEof() noexcept = 0;

	virtual void WasOutputSinkError(std::exception_ptr ep) noexcept = 0;
};

/**
 * Create a #WasOutputSink, which is on object that acts as #Istream
 * sink and sends all data to a WAS output pipe.
 */
WasOutputSink *
was_output_new(struct pool &pool, EventLoop &event_loop,
	       FileDescriptor fd, UnusedIstreamPtr input,
	       WasOutputSinkHandler &handler) noexcept;

/**
 * @return the total number of bytes written to the pipe
 */
uint64_t
was_output_free(WasOutputSink *data) noexcept;

/**
 * Check if we can provide the LENGTH header.
 *
 * @return the WasOutputSinkHandler::length() return value
 */
bool
was_output_check_length(WasOutputSink &output) noexcept;
