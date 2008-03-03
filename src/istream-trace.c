/*
 * This istream filter prints debug information to stderr.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-internal.h"

#include <assert.h>
#include <stdio.h>

struct istream_trace {
    struct istream output;
    istream_t input;
};


/*
 * istream handler
 *
 */

static size_t
trace_input_data(const void *data, size_t length, void *ctx)
{
    struct istream_trace *trace = ctx;
    size_t nbytes;

    fprintf(stderr, "data(%zu)\n", length);
    nbytes = istream_invoke_data(&trace->output, data, length);
    fprintf(stderr, "data(%zu)=%zu\n", length, nbytes);

    return nbytes;
}

static ssize_t
trace_input_direct(istream_direct_t type, int fd, size_t max_length, void *ctx)
{
    struct istream_trace *trace = ctx;
    ssize_t nbytes;

    fprintf(stderr, "direct(0x%x, %zd)\n",
            trace->output.handler_direct, max_length);
    nbytes = istream_invoke_direct(&trace->output, type, fd, 1);
    fprintf(stderr, "direct(0x%x, %zd)=%zd\n",
            trace->output.handler_direct, max_length, nbytes);

    return nbytes;
}

static void
trace_input_eof(void *ctx)
{
    struct istream_trace *trace = ctx;

    fprintf(stderr, "eof()\n");

    trace->input = NULL;
    istream_deinit_eof(&trace->output);
}

static void
trace_input_abort(void *ctx)
{
    struct istream_trace *trace = ctx;

    fprintf(stderr, "abort()\n");

    trace->input = NULL;
    istream_deinit_abort(&trace->output);
}

static const struct istream_handler trace_input_handler = {
    .data = trace_input_data,
    .direct = trace_input_direct,
    .eof = trace_input_eof,
    .abort = trace_input_abort,
};


/*
 * istream implementation
 *
 */

static inline struct istream_trace *
istream_to_trace(istream_t istream)
{
    return (struct istream_trace *)(((char*)istream) - offsetof(struct istream_trace, output));
}

static off_t 
istream_trace_available(istream_t istream, int partial)
{
    struct istream_trace *trace = istream_to_trace(istream);
    off_t available;

    fprintf(stderr, "available(%d)\n", partial);
    available = istream_available(trace->input, partial);
    fprintf(stderr, "available(%d)=%ld\n", partial, (long)available);

    return available;
}

static void
istream_trace_read(istream_t istream)
{
    struct istream_trace *trace = istream_to_trace(istream);

    fprintf(stderr, "read(0x%x)\n", trace->output.handler_direct);

    istream_handler_set_direct(trace->input, trace->output.handler_direct);
    istream_read(trace->input);
}

static void
istream_trace_close(istream_t istream)
{
    struct istream_trace *trace = istream_to_trace(istream);

    fprintf(stderr, "close()\n");

    istream_free_handler(&trace->input);
    istream_deinit_abort(&trace->output);
}

static const struct istream istream_trace = {
    .available = istream_trace_available,
    .read = istream_trace_read,
    .close = istream_trace_close,
};


/*
 * constructor
 *
 */

istream_t
istream_trace_new(pool_t pool, istream_t input)
{
    struct istream_trace *trace = istream_new_macro(pool, trace);

    assert(input != NULL);
    assert(!istream_has_handler(input));

    fprintf(stderr, "new()\n");

    istream_assign_handler(&trace->input, input,
                           &trace_input_handler, trace,
                           0);

    return istream_struct_cast(&trace->output);
}
