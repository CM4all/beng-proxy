// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "strmap.hxx"
#include "Completion.hxx"
#include "http/Status.hxx"

#include <utility> // for std::cmp_greater()

#include <assert.h>
#include <stddef.h>
#include <sys/types.h>
#include <stdint.h>

struct pool;
template<typename T> class ForeignFifoBuffer;

/**
 * A parser for the CGI response.
 *
 * - initialize with cgi_parser_init()
 *
 * - pass data received from the CGI program to
 * cgi_parser_feed_headers(), repeat with more data until it returns
 * C_ERROR or C_DONE
 *
 * - after C_DONE, call cgi_parser_get_headers()
 *
 * - use cgi_parser_available() and cgi_parser_consumed() while
 * transferring the response body
 */
struct CGIParser {
	HttpStatus status = HttpStatus::OK;

	/**
	 * The remaining number of bytes in the response body, -1 if
	 * unknown.
	 */
	off_t remaining = -1;

	StringMap headers;

	bool finished = false;

	/**
	 * Did the parser finish reading the response headers?
	 */
	bool AreHeadersFinished() const {
		return finished;
	}

	/**
	 * Run the CGI response header parser with data from the specified
	 * buffer.
	 *
	 * Throws exception on error.
	 *
	 * @param buffer a buffer containing data received from the CGI
	 * program; consumed data will automatically be removed
	 * @return DONE when the headers are finished (the remaining
	 * buffer contains the response body); PARTIAL or NONE when more
	 * header data is expected
	 */
	Completion FeedHeaders(struct pool &pool,
			       ForeignFifoBuffer<std::byte> &buffer);

	HttpStatus GetStatus() const noexcept {
		assert(finished);

		return status;
	}

	StringMap GetHeaders() && noexcept {
		assert(finished);

		return std::move(headers);
	}

	bool KnownLength() const {
		return remaining >= 0;
	}

	off_t GetAvailable() const {
		return remaining;
	}

	bool DoesRequireMore() const {
		return remaining > 0;
	}

	bool IsTooMuch(std::size_t length) const {
		return remaining != -1 && std::cmp_greater(length, remaining);
	}

	/**
	 * The caller has consumed data from the response body.
	 *
	 * @return true if the response body is finished
	 */
	bool BodyConsumed(std::size_t nbytes) {
		assert(nbytes > 0);

		if (remaining < 0)
			return false;

		assert(std::cmp_less_equal(nbytes, remaining));

		remaining -= nbytes;
		return remaining == 0;
	}

	bool IsEOF() const {
		return remaining == 0;
	}

private:
	/**
	 * Evaluate the response headers after the headers have been finalized
	 * by an empty line.
	 *
	 * Throws exception on error.
	 */
	Completion Finish(ForeignFifoBuffer<std::byte> &buffer);
};
