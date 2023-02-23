// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Parser.hxx"
#include "Error.hxx"
#include "http/HeaderParser.hxx"
#include "util/ForeignFifoBuffer.hxx"
#include "util/StringStrip.hxx"
#include "AllocatorPtr.hxx"

#include <string.h>
#include <stdlib.h>

inline Completion
CGIParser::Finish(ForeignFifoBuffer<std::byte> &buffer)
{
	/* parse the status */
	const char *p = headers.Remove("status");
	if (p != nullptr) {
		int i = atoi(p);
		if (http_status_is_valid((HttpStatus)i))
			status = (HttpStatus)i;
	}

	if (http_status_is_empty(status)) {
		/* there cannot be a response body */
		remaining = 0;
	} else {
		p = headers.Remove("content-length");
		if (p != nullptr) {
			/* parse the Content-Length response header */
			char *endptr;
			remaining = (off_t)strtoull(p, &endptr, 10);
			if (endptr == p || *endptr != 0 || remaining < 0)
				remaining = -1;
		} else
			/* unknown length */
			remaining = -1;
	}

	if (IsTooMuch(buffer.GetAvailable()))
		throw CgiError("too much data from CGI script");

	finished = true;
	return Completion::DONE;
}

Completion
CGIParser::FeedHeaders(struct pool &pool, ForeignFifoBuffer<std::byte> &buffer)
{
	assert(!AreHeadersFinished());

	auto r = buffer.Read();
	if (r.empty())
		return Completion::MORE;

	const char *data = (const char *)r.data();
	const char *data_end = data + r.size();

	/* parse each line until we stumble upon an empty one */
	const char *start = data, *end, *next = nullptr;
	while ((end = (const char *)memchr(start, '\n',
					   data_end - start)) != nullptr) {
		next = end + 1;

		end = StripRight(start, end);

		const size_t line_length = end - start;
		if (line_length == 0) {
			/* found an empty line, which is the separator between
			   headers and body */
			buffer.Consume(next - data);
			return Finish(buffer);
		}

		if (!header_parse_line(pool, headers, {start, line_length}))
			throw CgiError("Malformed CGI response header line");

		start = next;
	}

	if (next != nullptr) {
		buffer.Consume(next - data);
		return Completion::MORE;
	}

	if (buffer.IsFull()) {
		/* the buffer is full, and no header could be parsed: this
		   means the current header is too large for the buffer; bail
		   out */

		throw CgiError("CGI response header too long");
	}

	return Completion::MORE;
}
