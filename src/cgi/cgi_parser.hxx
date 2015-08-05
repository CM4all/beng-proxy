/*
 * Parsing CGI responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CGI_PARSER_HXX
#define BENG_PROXY_CGI_PARSER_HXX

#include "completion.h"
#include "glibfwd.hxx"

#include <http/status.h>

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
    http_status_t status;

    /**
     * The remaining number of bytes in the response body, -1 if
     * unknown.
     */
    off_t remaining;

    struct strmap *headers;

#ifndef NDEBUG
    bool finished;
#endif

    CGIParser(struct pool &pool);

    /**
     * Did the parser finish reading the response headers?
     */
    bool AreHeadersFinished() const {
        assert(finished == (headers == nullptr));

        return headers == nullptr;
    }

    /**
     * Run the CGI response header parser with data from the specified
     * buffer.
     *
     * @param buffer a buffer containing data received from the CGI
     * program; consumed data will automatically be removed
     * @return C_DONE when the headers are finished (the remaining buffer
     * contains the response body); C_PARTIAL or C_NONE when more header
     * data is expected; C_ERROR on error
     */
    enum completion FeedHeaders(struct pool &pool,
                                ForeignFifoBuffer<uint8_t> &buffer,
                                GError **error_r);

    http_status_t GetStatus() const {
        assert(finished);

        return status;
    }

    struct strmap &GetHeaders() {
        assert(headers != nullptr);
        assert(finished);

        struct strmap *_headers = headers;
        headers = nullptr;
        return *_headers;
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
    enum completion Finish(ForeignFifoBuffer<uint8_t> &buffer,
                           GError **error_r);
};

#endif
