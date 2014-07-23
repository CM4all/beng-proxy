/*
 * Run a CGI script.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cgi_client.hxx"
#include "cgi_quark.h"
#include "cgi_parser.hxx"
#include "pool.hxx"
#include "istream-buffer.h"
#include "async.hxx"
#include "header_parser.hxx"
#include "strutil.h"
#include "stopwatch.h"
#include "strmap.hxx"
#include "http_response.hxx"
#include "fb_pool.hxx"
#include "util/Cast.hxx"

#include <string.h>
#include <stdlib.h>

struct CGIClient {
    struct istream output;

    struct stopwatch *const stopwatch;

    struct istream *input;
    struct fifo_buffer *const buffer;

    CGIParser parser;

    /**
     * This flag is true while cgi_parse_headers() is calling
     * http_response_handler_ref::InvokeResponse().  In this case,
     * istream_read(cgi->input) is already up in the stack, and must
     * not be called again.
     */
    bool in_response_callback;

    bool had_input, had_output;

    struct async_operation operation;
    struct http_response_handler_ref handler;

    CGIClient(struct pool &pool, struct stopwatch *_stopwatch,
              struct istream &_input,
              const struct http_response_handler &_handler,
              void *handler_ctx,
              struct async_operation_ref &async_ref);

    /**
     * @return false if the connection has been closed
     */
    bool ReturnResponse();

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
    size_t FeedHeaders(const void *data, size_t length);

    /**
     * Call FeedHeaders() in a loop, to parse as much as possible.
     *
     * Caller must hold pool reference.
     */
    size_t FeedHeadersLoop(const char *data, size_t length);

    /**
     * Caller must hold pool reference.
     */
    size_t FeedHeadersCheck(const char *data, size_t length);

    size_t FeedBody(const char *data, size_t length);

    size_t Feed(const void *data, size_t length);

    void Abort();
};

inline bool
CGIClient::ReturnResponse()
{
    operation.Finished();

    http_status_t status = parser.GetStatus();
    struct strmap &headers = parser.GetHeaders();

    if (http_status_is_empty(status)) {
        /* this response does not have a response body, as indicated
           by the HTTP status code */

        stopwatch_event(stopwatch, "empty");
        stopwatch_dump(stopwatch);

        fb_pool_free(buffer);
        istream_free_handler(&input);
        handler.InvokeResponse(status, &headers, nullptr);
        pool_unref(output.pool);
        return false;
    } else if (parser.IsEOF()) {
        /* the response body is empty */

        stopwatch_event(stopwatch, "empty");
        stopwatch_dump(stopwatch);

        fb_pool_free(buffer);
        istream_free_handler(&input);
        handler.InvokeResponse(status, &headers,
                               istream_null_new(output.pool));
        pool_unref(output.pool);
        return false;
    } else {
        stopwatch_event(stopwatch, "headers");

        in_response_callback = true;
        handler.InvokeResponse(status, &headers, &output);
        in_response_callback = false;
        return input != nullptr;
    }
}

inline size_t
CGIClient::FeedHeaders(const void *data, size_t length)
{
    assert(!parser.AreHeadersFinished());

    size_t max_length;
    void *dest = fifo_buffer_write(buffer, &max_length);
    assert(dest != nullptr);

    if (length > max_length)
        length = max_length;

    memcpy(dest, data, length);
    fifo_buffer_append(buffer, length);

    GError *error = nullptr;
    enum completion c = parser.FeedHeaders(*output.pool, *buffer, &error);
    switch (c) {
    case C_DONE:
        /* the C_DONE status can only be triggered by new data that
           was just received; therefore, the amount of data still in
           the buffer (= response body) must be smaller */
        assert(fifo_buffer_available(buffer) < length);

        if (!ReturnResponse())
            return 0;

        /* don't consider data still in the buffer (= response body)
           as "consumed"; the caller will attempt to submit it to the
           response body handler */
        return length - fifo_buffer_available(buffer);

    case C_MORE:
        return length;

    case C_ERROR:
        fb_pool_free(buffer);
        istream_free_handler(&input);
        handler.InvokeAbort(error);
        pool_unref(output.pool);
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

inline size_t
CGIClient::FeedHeadersLoop(const char *data, size_t length)
{
    assert(length > 0);
    assert(!parser.AreHeadersFinished());

    size_t consumed = 0;

    do {
        size_t nbytes = FeedHeaders(data + consumed, length - consumed);
        if (nbytes == 0)
            break;

        consumed += nbytes;
    } while (consumed < length && !parser.AreHeadersFinished());

    if (input == nullptr)
        return 0;

    return consumed;
}

inline size_t
CGIClient::FeedHeadersCheck(const char *data, size_t length)
{
    size_t nbytes = FeedHeadersLoop(data, length);

    assert(nbytes == 0 || input != nullptr);
    assert(nbytes == 0 ||
           !parser.AreHeadersFinished() ||
           !parser.IsEOF());

    return nbytes;
}

inline size_t
CGIClient::FeedBody(const char *data, size_t length)
{
    if (parser.IsTooMuch(length)) {
        stopwatch_event(stopwatch, "malformed");
        stopwatch_dump(stopwatch);

        fb_pool_free(buffer);
        istream_free_handler(&input);

        GError *error =
            g_error_new_literal(cgi_quark(), 0,
                                "too much data from CGI script");
        istream_deinit_abort(&output, error);
        return 0;
    }

    had_output = true;

    size_t nbytes = istream_invoke_data(&output, data, length);
    if (nbytes > 0 && parser.BodyConsumed(nbytes)) {
        stopwatch_event(stopwatch, "end");
        stopwatch_dump(stopwatch);

        fb_pool_free(buffer);
        istream_free_handler(&input);
        istream_deinit_eof(&output);
        return 0;
    }

    return nbytes;
}

inline size_t
CGIClient::Feed(const void *data, size_t length)
{
    assert(input != nullptr);

    had_input = true;

    if (!parser.AreHeadersFinished()) {
        const ScopePoolRef ref(*output.pool TRACE_ARGS);

        size_t nbytes = FeedHeadersCheck((const char *)data, length);

        if (nbytes > 0 && nbytes < length &&
            parser.AreHeadersFinished()) {
            /* the headers are finished; now begin sending the
               response body */
            size_t nbytes2 = FeedBody((const char *)data + nbytes,
                                      length - nbytes);
            if (nbytes2 > 0)
                /* more data was consumed */
                nbytes += nbytes2;
            else if (input == nullptr)
                /* the connection was closed, must return 0 */
                nbytes = 0;
        }

        return nbytes;
    } else {
        return FeedBody((const char *)data, length);
    }
}

/*
 * input handler
 *
 */

static size_t
cgi_input_data(const void *data, size_t length, void *ctx)
{
    CGIClient *cgi = (CGIClient *)ctx;

    return cgi->Feed(data, length);
}

static ssize_t
cgi_input_direct(enum istream_direct type, int fd, size_t max_length,
                 void *ctx)
{
    CGIClient *cgi = (CGIClient *)ctx;

    assert(cgi->parser.AreHeadersFinished());

    cgi->had_input = true;
    cgi->had_output = true;

    if (cgi->parser.KnownLength() &&
        (off_t)max_length > cgi->parser.GetAvailable())
        max_length = (size_t)cgi->parser.GetAvailable();

    ssize_t nbytes = istream_invoke_direct(&cgi->output, type, fd, max_length);
    if (nbytes > 0 && cgi->parser.BodyConsumed(nbytes)) {
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
    CGIClient *cgi = (CGIClient *)ctx;

    cgi->input = nullptr;

    if (!cgi->parser.AreHeadersFinished()) {
        stopwatch_event(cgi->stopwatch, "malformed");
        stopwatch_dump(cgi->stopwatch);

        assert(!istream_has_handler(istream_struct_cast(&cgi->output)));

        fb_pool_free(cgi->buffer);

        GError *error =
            g_error_new_literal(cgi_quark(), 0,
                                "premature end of headers from CGI script");
        cgi->handler.InvokeAbort(error);
        pool_unref(cgi->output.pool);
    } else if (cgi->parser.DoesRequireMore()) {
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
    CGIClient *cgi = (CGIClient *)ctx;

    stopwatch_event(cgi->stopwatch, "abort");
    stopwatch_dump(cgi->stopwatch);

    cgi->input = nullptr;

    if (!cgi->parser.AreHeadersFinished()) {
        /* the response hasn't been sent yet: notify the response
           handler */
        assert(!istream_has_handler(istream_struct_cast(&cgi->output)));

        fb_pool_free(cgi->buffer);

        g_prefix_error(&error, "CGI request body failed: ");
        cgi->handler.InvokeAbort(error);
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

static inline CGIClient *
istream_to_cgi(struct istream *istream)
{
    return &ContainerCast2(*istream, &CGIClient::output);
}

static off_t
istream_cgi_available(struct istream *istream, bool partial)
{
    CGIClient *cgi = istream_to_cgi(istream);

    if (cgi->parser.KnownLength())
        return cgi->parser.GetAvailable();

    if (cgi->input == nullptr)
        return 0;

    if (cgi->in_response_callback)
        /* this condition catches the case in cgi_parse_headers():
           http_response_handler_ref::InvokeResponse() might
           recursively call istream_read(cgi->input) */
        return (off_t)-1;

    return istream_available(cgi->input, partial);
}

static void
istream_cgi_read(struct istream *istream)
{
    CGIClient *cgi = istream_to_cgi(istream);

    if (cgi->input != nullptr) {
        istream_handler_set_direct(cgi->input, cgi->output.handler_direct);

        /* this condition catches the case in cgi_parse_headers():
           http_response_handler_ref::InvokeResponse() might
           recursively call istream_read(cgi->input) */
        if (cgi->in_response_callback) {
            return;
        }

        const ScopePoolRef ref(*cgi->output.pool TRACE_ARGS);

        cgi->had_output = false;
        do {
            cgi->had_input = false;
            istream_read(cgi->input);
        } while (cgi->input != nullptr && cgi->had_input &&
                 !cgi->had_output);
    }
}

static void
istream_cgi_close(struct istream *istream)
{
    CGIClient *cgi = istream_to_cgi(istream);

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

inline void
CGIClient::Abort()
{
    assert(input != nullptr);

    fb_pool_free(buffer);
    istream_close_handler(input);
    pool_unref(output.pool);
}


/*
 * constructor
 *
 */

inline
CGIClient::CGIClient(struct pool &pool, struct stopwatch *_stopwatch,
                     struct istream &_input,
                     const struct http_response_handler &_handler,
                     void *handler_ctx,
                     struct async_operation_ref &async_ref)
    :stopwatch(_stopwatch),
     buffer(fb_pool_alloc()),
     parser(pool)
{
    istream_init(&output, &istream_cgi, &pool);

    istream_assign_handler(&input, &_input,
                           &cgi_input_handler, this, 0);

    handler.Set(_handler, handler_ctx);

    operation.Init2<CGIClient>();
    async_ref.Set(operation);

    istream_read(input);
}

void
cgi_client_new(struct pool *pool, struct stopwatch *stopwatch,
               struct istream *input,
               const struct http_response_handler *handler, void *handler_ctx,
               struct async_operation_ref *async_ref)
{
    assert(pool != nullptr);
    assert(input != nullptr);
    assert(handler != nullptr);

    NewFromPool<CGIClient>(*pool, *pool, stopwatch, *input,
                           *handler, handler_ctx, *async_ref);
}
