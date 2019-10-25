/*
 * Copyright 2007-2019 Content Management AG
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

#include "strmap.hxx"
#include "Completion.hxx"
#include "http/Status.h"

#include <assert.h>
#include <stddef.h>
#include <sys/types.h>
#include <stdint.h>

struct pool;
class StringMap;
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
    http_status_t status = HTTP_STATUS_OK;

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
                           ForeignFifoBuffer<uint8_t> &buffer);

    http_status_t GetStatus() const {
        assert(finished);

        return status;
    }

    StringMap &GetHeaders() {
        assert(finished);

        return headers;
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

    bool IsTooMuch(size_t length) const {
        return remaining != -1 && (off_t)length > remaining;
    }

    /**
     * The caller has consumed data from the response body.
     *
     * @return true if the response body is finished
     */
    bool BodyConsumed(size_t nbytes) {
        assert(nbytes > 0);

        if (remaining < 0)
            return false;

        assert((off_t)nbytes <= remaining);

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
    Completion Finish(ForeignFifoBuffer<uint8_t> &buffer);
};
