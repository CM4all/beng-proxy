/*
 * Run a CGI script.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cgi_client.hxx"
#include "cgi_quark.h"
#include "cgi_parser.hxx"
#include "istream-buffer.h"
#include "async.hxx"
#include "header_parser.hxx"
#include "strutil.h"
#include "stopwatch.h"
#include "strmap.hxx"
#include "http_response.hxx"
#include "fb_pool.h"
#include "util/Cast.hxx"

#include <string.h>
#include <stdlib.h>

struct cgi {
    struct istream output;

    struct stopwatch *stopwatch;

    struct istream *input;
    struct fifo_buffer *buffer;

    struct cgi_parser parser;

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

/**
 * @return false if the connection has been closed
 */
static bool
cgi_return_response(struct cgi *cgi)
{
    cgi->async.Finished();

    http_status_t status = cgi_parser_get_status(&cgi->parser);
    struct strmap *headers = cgi_parser_get_headers(&cgi->parser);

    if (http_status_is_empty(status)) {
        /* this response does not have a response body, as indicated
           by the HTTP status code */

        stopwatch_event(cgi->stopwatch, "empty");
        stopwatch_dump(cgi->stopwatch);

        fb_pool_free(cgi->buffer);
        istream_free_handler(&cgi->input);
        http_response_handler_invoke_response(&cgi->handler, status, headers,
                                              nullptr);
        pool_unref(cgi->output.pool);
        return false;
    } else if (cgi_parser_eof(&cgi->parser)) {
        /* the response body is empty */

        stopwatch_event(cgi->stopwatch, "empty");
        stopwatch_dump(cgi->stopwatch);

        fb_pool_free(cgi->buffer);
        istream_free_handler(&cgi->input);
        http_response_handler_invoke_response(&cgi->handler, status, headers,
                                              istream_null_new(cgi->output.pool));
        pool_unref(cgi->output.pool);
        return false;
    } else {
        stopwatch_event(cgi->stopwatch, "headers");

        cgi->in_response_callback = true;
        http_response_handler_invoke_response(&cgi->handler, status, headers,
                                              istream_struct_cast(&cgi->output));
        cgi->in_response_callback = false;
        return cgi->input != nullptr;
    }
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
    assert(!cgi_parser_headers_finished(&cgi->parser));

    size_t max_length;
    void *dest = fifo_buffer_write(cgi->buffer, &max_length);
    assert(dest != nullptr);

    if (length > max_length)
        length = max_length;

    memcpy(dest, data, length);
    fifo_buffer_append(cgi->buffer, length);

    GError *error = nullptr;
    enum completion c = cgi_parser_feed_headers(cgi->output.pool, &cgi->parser,
                                                cgi->buffer, &error);
    switch (c) {
    case C_DONE:
        /* the C_DONE status can only be triggered by new data that
           was just received; therefore, the amount of data still in
           the buffer (= response body) must be smaller */
        assert(fifo_buffer_available(cgi->buffer) < length);

        if (!cgi_return_response(cgi))
            return 0;

        /* don't consider data still in the buffer (= response body)
           as "consumed"; the caller will attempt to submit it to the
           response body handler */
        return length - fifo_buffer_available(cgi->buffer);

    case C_MORE:
        return length;

    case C_ERROR:
        fb_pool_free(cgi->buffer);
        istream_free_handler(&cgi->input);
        http_response_handler_invoke_abort(&cgi->handler, error);
        pool_unref(cgi->output.pool);
        return 0;

    case C_CLOSED:
        /* unreachable */
        assert(false);
        return 0;
    }

    /* unreachable */
    assert(false);
    return 0;
}

/**
 * Call cgi_feed_headers() in a loop, to parse as much as possible.
 *
 * Caller must hold pool reference.
 */
static size_t
cgi_feed_headers2(struct cgi *cgi, const char *data, size_t length)
{
    assert(length > 0);
    assert(!cgi_parser_headers_finished(&cgi->parser));

    size_t consumed = 0;

    do {
        size_t nbytes = cgi_feed_headers(cgi, data + consumed,
                                         length - consumed);
        if (nbytes == 0)
            break;

        consumed += nbytes;
    } while (consumed < length && !cgi_parser_headers_finished(&cgi->parser));

    if (cgi->input == nullptr)
        return 0;

    return consumed;
}

/**
 * Caller must hold pool reference.
 */
static size_t
cgi_feed_headers3(struct cgi *cgi, const char *data, size_t length)
{
    size_t nbytes = cgi_feed_headers2(cgi, data, length);

    assert(nbytes == 0 || cgi->input != nullptr);
    assert(nbytes == 0 ||
           !cgi_parser_headers_finished(&cgi->parser) ||
           !cgi_parser_eof(&cgi->parser));

    return nbytes;
}

static size_t
cgi_feed_body(struct cgi *cgi, const char *data, size_t length)
{
    if (cgi_parser_is_too_much(&cgi->parser, length)) {
        stopwatch_event(cgi->stopwatch, "malformed");
        stopwatch_dump(cgi->stopwatch);

        fb_pool_free(cgi->buffer);
        istream_free_handler(&cgi->input);

        GError *error =
            g_error_new_literal(cgi_quark(), 0,
                                "too much data from CGI script");
        istream_deinit_abort(&cgi->output, error);
        return 0;
    }

    cgi->had_output = true;

    size_t nbytes = istream_invoke_data(&cgi->output, data, length);
    if (nbytes > 0 && cgi_parser_body_consumed(&cgi->parser, nbytes)) {
        stopwatch_event(cgi->stopwatch, "end");
        stopwatch_dump(cgi->stopwatch);

        fb_pool_free(cgi->buffer);
        istream_free_handler(&cgi->input);
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
    struct cgi *cgi = (struct cgi *)ctx;

    assert(cgi->input != nullptr);

    cgi->had_input = true;

    if (!cgi_parser_headers_finished(&cgi->parser)) {
        pool_ref(cgi->output.pool);

        size_t nbytes = cgi_feed_headers3(cgi, (const char *)data, length);

        if (nbytes > 0 && nbytes < length &&
            cgi_parser_headers_finished(&cgi->parser)) {
            /* the headers are finished; now begin sending the
               response body */
            size_t nbytes2 = cgi_feed_body(cgi, (const char *)data + nbytes,
                                           length - nbytes);
            if (nbytes2 > 0)
                /* more data was consumed */
                nbytes += nbytes2;
            else if (cgi->input == nullptr)
                /* the connection was closed, must return 0 */
                nbytes = 0;
        }

        pool_unref(cgi->output.pool);

        return nbytes;
    } else {
        return cgi_feed_body(cgi, (const char *)data, length);
    }
}

static ssize_t
cgi_input_direct(enum istream_direct type, int fd, size_t max_length,
                 void *ctx)
{
    struct cgi *cgi = (struct cgi *)ctx;

    assert(cgi_parser_headers_finished(&cgi->parser));

    cgi->had_input = true;
    cgi->had_output = true;

    if (cgi_parser_known_length(&cgi->parser) &&
        (off_t)max_length > cgi_parser_available(&cgi->parser))
        max_length = (size_t)cgi_parser_available(&cgi->parser);

    ssize_t nbytes = istream_invoke_direct(&cgi->output, type, fd, max_length);
    if (nbytes > 0 && cgi_parser_body_consumed(&cgi->parser, nbytes)) {
        stopwatch_event(cgi->stopwatch, "end");
        stopwatch_dump(cgi->stopwatch);

        fb_pool_free(cgi->buffer);
        istream_close_handler(cgi->input);
        istream_deinit_eof(&cgi->output);
        return ISTREAM_RESULT_CLOSED;
    }

    return nbytes;
}

static void
cgi_input_eof(void *ctx)
{
    struct cgi *cgi = (struct cgi *)ctx;

    cgi->input = nullptr;

    if (!cgi_parser_headers_finished(&cgi->parser)) {
        stopwatch_event(cgi->stopwatch, "malformed");
        stopwatch_dump(cgi->stopwatch);

        assert(!istream_has_handler(istream_struct_cast(&cgi->output)));

        fb_pool_free(cgi->buffer);

        GError *error =
            g_error_new_literal(cgi_quark(), 0,
                                "premature end of headers from CGI script");
        http_response_handler_invoke_abort(&cgi->handler, error);
        pool_unref(cgi->output.pool);
    } else if (cgi_parser_requires_more(&cgi->parser)) {
        stopwatch_event(cgi->stopwatch, "malformed");
        stopwatch_dump(cgi->stopwatch);

        fb_pool_free(cgi->buffer);

        GError *error =
            g_error_new_literal(cgi_quark(), 0,
                                "premature end of response body from CGI script");
        istream_deinit_abort(&cgi->output, error);
    } else {
        stopwatch_event(cgi->stopwatch, "end");
        stopwatch_dump(cgi->stopwatch);

        fb_pool_free(cgi->buffer);
        istream_deinit_eof(&cgi->output);
    }
}

static void
cgi_input_abort(GError *error, void *ctx)
{
    struct cgi *cgi = (struct cgi *)ctx;

    stopwatch_event(cgi->stopwatch, "abort");
    stopwatch_dump(cgi->stopwatch);

    cgi->input = nullptr;

    if (!cgi_parser_headers_finished(&cgi->parser)) {
        /* the response hasn't been sent yet: notify the response
           handler */
        assert(!istream_has_handler(istream_struct_cast(&cgi->output)));

        fb_pool_free(cgi->buffer);

        g_prefix_error(&error, "CGI request body failed: ");
        http_response_handler_invoke_abort(&cgi->handler, error);
        pool_unref(cgi->output.pool);
    } else {
        /* response has been sent: abort only the output stream */
        fb_pool_free(cgi->buffer);
        istream_deinit_abort(&cgi->output, error);
    }
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
    return ContainerCast(istream, struct cgi, output);
}

static off_t
istream_cgi_available(struct istream *istream, bool partial)
{
    struct cgi *cgi = istream_to_cgi(istream);

    if (cgi_parser_known_length(&cgi->parser))
        return cgi_parser_available(&cgi->parser);

    if (cgi->input == nullptr)
        return 0;

    if (cgi->in_response_callback)
        /* this condition catches the case in cgi_parse_headers():
           http_response_handler_invoke_response() might recursively call
           istream_read(cgi->input) */
        return (off_t)-1;

    return istream_available(cgi->input, partial);
}

static void
istream_cgi_read(struct istream *istream)
{
    struct cgi *cgi = istream_to_cgi(istream);

    if (cgi->input != nullptr) {
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
        } while (cgi->input != nullptr && cgi->had_input &&
                 !cgi->had_output);

        pool_unref(cgi->output.pool);
    }
}

static void
istream_cgi_close(struct istream *istream)
{
    struct cgi *cgi = istream_to_cgi(istream);

    fb_pool_free(cgi->buffer);

    if (cgi->input != nullptr)
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
    return ContainerCast(ao, struct cgi, async);
}

static void
cgi_async_abort(struct async_operation *ao)
{
    struct cgi *cgi = async_to_cgi(ao);

    assert(cgi->input != nullptr);

    fb_pool_free(cgi->buffer);
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
    assert(pool != nullptr);
    assert(input != nullptr);
    assert(handler != nullptr);

    struct cgi *cgi = (struct cgi *)istream_new(pool, &istream_cgi, sizeof(*cgi));
    cgi->stopwatch = stopwatch;
    istream_assign_handler(&cgi->input, input,
                           &cgi_input_handler, cgi, 0);

    cgi->buffer = fb_pool_alloc();

    cgi_parser_init(pool, &cgi->parser);

    http_response_handler_set(&cgi->handler, handler, handler_ctx);

    cgi->async.Init(cgi_async_operation);
    async_ref->Set(cgi->async);

    istream_read(input);
}
