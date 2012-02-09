/*
 * Run a CGI script.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cgi-client.h"
#include "cgi-quark.h"
#include "istream-buffer.h"
#include "async.h"
#include "header-parser.h"
#include "strutil.h"
#include "stopwatch.h"
#include "strmap.h"
#include "http-response.h"

#include <string.h>
#include <stdlib.h>

struct cgi {
    struct istream output;

    struct stopwatch *stopwatch;

    struct istream *input;
    struct fifo_buffer *buffer;
    struct strmap *headers;

    /**
     * The remaining number of bytes in the response body, -1 if
     * unknown.
     */
    off_t remaining;

    /**
     * This flag is true while cgi_parse_headers() is calling
     * http_response_handler_invoke_response().  In this case,
     * istream_read(cgi->input) is already up in the stack, and must
     * not be called again.
     */
    bool in_response_callback;

    bool had_input, had_output;

    struct async_operation async;
    struct http_response_handler_ref handler;
};

static bool
cgi_handle_line(struct cgi *cgi, const char *line, size_t length)
{
    assert(cgi != NULL);
    assert(cgi->headers != NULL);
    assert(line != NULL);

    if (length > 0) {
        header_parse_line(cgi->output.pool, cgi->headers,
                          line, length);
        return false;
    } else
        return true;
}

static void
cgi_return_response(struct cgi *cgi)
{
    struct strmap *headers;

    async_operation_finished(&cgi->async);

    headers = cgi->headers;
    cgi->headers = NULL;
    cgi->in_response_callback = true;

    http_status_t status = HTTP_STATUS_OK;
    const char *p = strmap_remove(headers, "status");
    if (p != NULL) {
        int i = atoi(p);
        if (http_status_is_valid(i))
            status = (http_status_t)i;
    }

    if (http_status_is_empty(status)) {
        /* this response does not have a response body, as indicated
           by the HTTP status code */

        stopwatch_event(cgi->stopwatch, "empty");
        stopwatch_dump(cgi->stopwatch);

        istream_free_handler(&cgi->input);
        http_response_handler_invoke_response(&cgi->handler,
                                              status, headers,
                                              NULL);
        pool_unref(cgi->output.pool);
    } else {
        stopwatch_event(cgi->stopwatch, "headers");

        p = strmap_remove(headers, "content-length");
        if (p != NULL) {
            char *endptr;
            cgi->remaining = (off_t)strtoull(p, &endptr, 10);
            if (endptr == p || *endptr != 0 || cgi->remaining < 0)
                cgi->remaining = -1;
        } else
            cgi->remaining = -1;

        if (cgi->remaining != -1) {
            if ((off_t)fifo_buffer_available(cgi->buffer) > cgi->remaining) {
                istream_free_handler(&cgi->input);

                GError *error =
                    g_error_new_literal(cgi_quark(), 0,
                                        "too much data from CGI script");
                http_response_handler_invoke_abort(&cgi->handler, error);
                cgi->in_response_callback = false;
                pool_unref(cgi->output.pool);
                return;
            }

            cgi->remaining -= fifo_buffer_available(cgi->buffer);
        }

        http_response_handler_invoke_response(&cgi->handler,
                                              status, headers,
                                              istream_struct_cast(&cgi->output));
    }

    cgi->in_response_callback = false;
}

static void
cgi_parse_headers(struct cgi *cgi)
{
    size_t length;
    const char *buffer = fifo_buffer_read(cgi->buffer, &length);
    if (buffer == NULL)
        return;

    assert(length > 0);
    const char *buffer_end = buffer + length;

    bool finished = false;
    const char *start = buffer, *end, *next = NULL;
    while ((end = memchr(start, '\n', buffer_end - start)) != NULL) {
        next = end + 1;
        --end;
        while (end >= start && char_is_whitespace(*end))
            --end;

        finished = cgi_handle_line(cgi, start, end - start + 1);
        if (finished)
            break;

        start = next;
    }

    if (next == NULL)
        return;

    fifo_buffer_consume(cgi->buffer, next - buffer);

    if (finished)
        cgi_return_response(cgi);
}

/**
 * Feed data into the input buffer and continue parsing response
 * headers from it.  After this function returns, the response may
 * have been delivered to the response handler, and the caller should
 * post the rest of the specified buffer to the response body stream.
 *
 * Caller must hold pool reference.
 *
 * @return the number of bytes consumed from the specified buffer
 * (moved to the input buffer), 0 if the object has been closed
 */
static size_t
cgi_feed_headers(struct cgi *cgi, const void *data, size_t length)
{
    size_t max_length;
    void *dest = fifo_buffer_write(cgi->buffer, &max_length);
    assert(dest != NULL);

    if (length > max_length)
        length = max_length;

    memcpy(dest, data, length);
    fifo_buffer_append(cgi->buffer, length);

    cgi_parse_headers(cgi);

    /* we check cgi->input here because this is our indicator that
       cgi->output has been closed; since we are in the cgi->input
       data handler, this is the only reason why cgi->input can be
       NULL */
    if (cgi->input == NULL)
        return 0;

    if (fifo_buffer_full(cgi->buffer)) {
        /* the buffer is full, and no header could be parsed: this
           means the current header is too large for the buffer; bail
           out */
        istream_free_handler(&cgi->input);

        GError *error =
            g_error_new_literal(cgi_quark(), 0,
                                "CGI response header too long");
        http_response_handler_invoke_abort(&cgi->handler, error);
        pool_unref(cgi->output.pool);
        return 0;
    }

    return length;
}

/**
 * Call cgi_feed_headers() in a loop, to parse as much as possible.
 */
static size_t
cgi_feed_headers2(struct cgi *cgi, const char *data, size_t length)
{
    size_t consumed = 0;

    while (consumed < length) {
        size_t nbytes = cgi_feed_headers(cgi, data + consumed,
                                         length - consumed);
        if (nbytes == 0)
            break;

        consumed += nbytes;
    }

    if (cgi->input == NULL)
        return 0;

    return consumed;
}

static size_t
cgi_feed_headers3(struct cgi *cgi, const char *data, size_t length)
{
    size_t nbytes = cgi_feed_headers2(cgi, data, length);
    if (nbytes == 0)
        return 0;

    assert(cgi->input != NULL);

    if (cgi->headers == NULL && !fifo_buffer_empty(cgi->buffer)) {
        size_t consumed = istream_buffer_send(&cgi->output, cgi->buffer);
        if (consumed == 0 && cgi->input == NULL)
            /* we have been closed, bail out */
            return 0;

        cgi->had_output = true;
    }

    if (cgi->headers == NULL &&
        cgi->remaining == 0 && fifo_buffer_empty(cgi->buffer)) {
        /* the response body is already finished (probably because
           it was present, but empty); submit that result to the
           handler immediately */

        stopwatch_event(cgi->stopwatch, "end");
        stopwatch_dump(cgi->stopwatch);

        istream_close_handler(cgi->input);
        istream_deinit_eof(&cgi->output);
        return 0;
    }

    return nbytes;
}

/*
 * input handler
 *
 */

static size_t
cgi_input_data(const void *data, size_t length, void *ctx)
{
    struct cgi *cgi = ctx;

    assert(cgi->input != NULL);

    cgi->had_input = true;

    if (cgi->headers != NULL) {
        pool_ref(cgi->output.pool);

        size_t nbytes = cgi_feed_headers3(cgi, data, length);

        pool_unref(cgi->output.pool);

        return nbytes;
    } else {
        if (cgi->remaining != -1 && (off_t)length > cgi->remaining) {
            stopwatch_event(cgi->stopwatch, "malformed");
            stopwatch_dump(cgi->stopwatch);

            istream_close_handler(cgi->input);

            GError *error =
                g_error_new_literal(cgi_quark(), 0,
                                    "too much data from CGI script");
            istream_deinit_abort(&cgi->output, error);
            return 0;
        }

        if (cgi->buffer != NULL) {
            size_t rest = istream_buffer_consume(&cgi->output, cgi->buffer);
            if (rest > 0)
                return 0;

            cgi->buffer = NULL;
        }

        cgi->had_output = true;

        size_t nbytes = istream_invoke_data(&cgi->output, data, length);
        if (nbytes > 0 && cgi->remaining != -1) {
            cgi->remaining -= nbytes;

            if (cgi->remaining == 0) {
                stopwatch_event(cgi->stopwatch, "end");
                stopwatch_dump(cgi->stopwatch);

                istream_close_handler(cgi->input);
                istream_deinit_eof(&cgi->output);
                return 0;
            }
        }

        return nbytes;
    }
}

static ssize_t
cgi_input_direct(istream_direct_t type, int fd, size_t max_length, void *ctx)
{
    struct cgi *cgi = ctx;

    assert(cgi->headers == NULL);

    cgi->had_input = true;
    cgi->had_output = true;

    if (cgi->remaining == 0) {
        stopwatch_event(cgi->stopwatch, "end");
        stopwatch_dump(cgi->stopwatch);

        istream_close_handler(cgi->input);
        istream_deinit_eof(&cgi->output);
        return ISTREAM_RESULT_CLOSED;
    }

    if (cgi->remaining != -1 && (off_t)max_length > cgi->remaining)
        max_length = (size_t)cgi->remaining;

    ssize_t nbytes = istream_invoke_direct(&cgi->output, type, fd, max_length);
    if (nbytes > 0 && cgi->remaining != -1) {
        cgi->remaining -= nbytes;

        if (cgi->remaining == 0) {
            stopwatch_event(cgi->stopwatch, "end");
            stopwatch_dump(cgi->stopwatch);

            istream_close_handler(cgi->input);
            istream_deinit_eof(&cgi->output);
            return ISTREAM_RESULT_CLOSED;
        }
    }

    return nbytes;
}

static void
cgi_input_eof(void *ctx)
{
    struct cgi *cgi = ctx;

    cgi->input = NULL;

    if (cgi->headers != NULL) {
        stopwatch_event(cgi->stopwatch, "malformed");
        stopwatch_dump(cgi->stopwatch);

        assert(!istream_has_handler(istream_struct_cast(&cgi->output)));

        GError *error =
            g_error_new_literal(cgi_quark(), 0,
                                "premature end of headers from CGI script");
        http_response_handler_invoke_abort(&cgi->handler, error);
        pool_unref(cgi->output.pool);
    } else if (cgi->remaining > 0) {
        stopwatch_event(cgi->stopwatch, "malformed");
        stopwatch_dump(cgi->stopwatch);

        GError *error =
            g_error_new_literal(cgi_quark(), 0,
                                "premature end of response body from CGI script");
        istream_deinit_abort(&cgi->output, error);
    } else if (cgi->buffer == NULL || fifo_buffer_empty(cgi->buffer)) {
        stopwatch_event(cgi->stopwatch, "end");
        stopwatch_dump(cgi->stopwatch);

        istream_deinit_eof(&cgi->output);
    }
}

static void
cgi_input_abort(GError *error, void *ctx)
{
    struct cgi *cgi = ctx;

    stopwatch_event(cgi->stopwatch, "abort");
    stopwatch_dump(cgi->stopwatch);

    cgi->input = NULL;

    if (cgi->headers != NULL) {
        /* the response hasn't been sent yet: notify the response
           handler */
        assert(!istream_has_handler(istream_struct_cast(&cgi->output)));

        g_prefix_error(&error, "CGI request body failed: ");
        http_response_handler_invoke_abort(&cgi->handler, error);
        pool_unref(cgi->output.pool);
    } else
        /* response has been sent: abort only the output stream */
        istream_deinit_abort(&cgi->output, error);
}

static const struct istream_handler cgi_input_handler = {
    .data = cgi_input_data,
    .direct = cgi_input_direct,
    .eof = cgi_input_eof,
    .abort = cgi_input_abort,
};


/*
 * istream implementation
 *
 */

static inline struct cgi *
istream_to_cgi(struct istream *istream)
{
    return (struct cgi *)(((char*)istream) - offsetof(struct cgi, output));
}

static off_t
istream_cgi_available(struct istream *istream, bool partial)
{
    struct cgi *cgi = istream_to_cgi(istream);

    size_t length;
    if (cgi->buffer != NULL) {
        length = fifo_buffer_available(cgi->buffer);
    } else
        length = 0;

    if (cgi->remaining != -1)
        return (off_t)length + cgi->remaining;

    if (cgi->input == NULL)
        return length;

    if (cgi->in_response_callback) {
        /* this condition catches the case in cgi_parse_headers():
           http_response_handler_invoke_response() might recursively call
           istream_read(cgi->input) */
        if (partial)
            return length;
        else
            return (off_t)-1;
    }

    off_t available = istream_available(cgi->input, partial);
    if (available == (off_t)-1) {
        if (partial)
            return length;
        else
            return (off_t)-1;
    }

    return (off_t)length + available;
}

static void
istream_cgi_read(struct istream *istream)
{
    struct cgi *cgi = istream_to_cgi(istream);

    if (cgi->input != NULL) {
        istream_handler_set_direct(cgi->input, cgi->output.handler_direct);

        /* this condition catches the case in cgi_parse_headers():
           http_response_handler_invoke_response() might recursively call
           istream_read(cgi->input) */
        if (cgi->in_response_callback) {
            return;
        }

        pool_ref(cgi->output.pool);

        cgi->had_output = false;
        do {
            cgi->had_input = false;
            istream_read(cgi->input);
        } while (cgi->input != NULL && cgi->had_input &&
                 !cgi->had_output);

        pool_unref(cgi->output.pool);
    } else {
        size_t rest = istream_buffer_consume(&cgi->output, cgi->buffer);
        if (rest == 0) {
            stopwatch_event(cgi->stopwatch, "end");
            stopwatch_dump(cgi->stopwatch);

            istream_deinit_eof(&cgi->output);
        }
    }
}

static void
istream_cgi_close(struct istream *istream)
{
    struct cgi *cgi = istream_to_cgi(istream);

    if (cgi->input != NULL)
        istream_free_handler(&cgi->input);

    istream_deinit(&cgi->output);
}

static const struct istream_class istream_cgi = {
    .available = istream_cgi_available,
    .read = istream_cgi_read,
    .close = istream_cgi_close,
};


/*
 * async operation
 *
 */

static struct cgi *
async_to_cgi(struct async_operation *ao)
{
    return (struct cgi*)(((char*)ao) - offsetof(struct cgi, async));
}

static void
cgi_async_abort(struct async_operation *ao)
{
    struct cgi *cgi = async_to_cgi(ao);

    assert(cgi->input != NULL);

    istream_close_handler(cgi->input);
    pool_unref(cgi->output.pool);
}

static const struct async_operation_class cgi_async_operation = {
    .abort = cgi_async_abort,
};


/*
 * constructor
 *
 */

void
cgi_client_new(struct pool *pool, struct stopwatch *stopwatch,
               struct istream *input,
               const struct http_response_handler *handler, void *handler_ctx,
               struct async_operation_ref *async_ref)
{
    assert(pool != NULL);
    assert(input != NULL);
    assert(handler != NULL);

    struct cgi *cgi = (struct cgi *)istream_new(pool, &istream_cgi, sizeof(*cgi));
    cgi->stopwatch = stopwatch;
    istream_assign_handler(&cgi->input, input,
                           &cgi_input_handler, cgi, 0);

    cgi->buffer = fifo_buffer_new(pool, 4096);
    cgi->headers = strmap_new(pool, 32);

    http_response_handler_set(&cgi->handler, handler, handler_ctx);

    async_init(&cgi->async, &cgi_async_operation);
    async_ref_set(async_ref, &cgi->async);

    istream_read(input);
}
