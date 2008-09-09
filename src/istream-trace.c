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


static void
trace_data(const void *data0, size_t length)
{
    const char *data = data0;
    size_t i;

    fputc('"', stderr);
    for (i = 0; i < length; ++i) {
        if (data[i] == '\n')
            fputs("\\n", stderr);
        else if (data[i] == '\r')
            fputs("\\r", stderr);
        else if (data[i] == 0)
            fputs("\\0", stderr);
        else if (data[i] == '"')
            fputs("\\\"", stderr);
        else
            fputc(data[i], stderr);
    }
    fputs("\"\n", stderr);
}

/*
 * istream handler
 *
 */

static size_t
trace_input_data(const void *data, size_t length, void *ctx)
{
    struct istream_trace *trace = ctx;
    size_t nbytes;

    fprintf(stderr, "%p data(%zu)\n", (const void*)trace, length);
    trace_data(data, length);
    nbytes = istream_invoke_data(&trace->output, data, length);
    fprintf(stderr, "%p data(%zu)=%zu\n", (const void*)trace, length, nbytes);

    return nbytes;
}

static ssize_t
trace_input_direct(istream_direct_t type, int fd, size_t max_length, void *ctx)
{
    struct istream_trace *trace = ctx;
    ssize_t nbytes;

    fprintf(stderr, "%p direct(0x%x, %zd)\n", (const void*)trace,
            trace->output.handler_direct, max_length);
    nbytes = istream_invoke_direct(&trace->output, type, fd, 1);
    fprintf(stderr, "%p direct(0x%x, %zd)=%zd\n", (const void*)trace,
            trace->output.handler_direct, max_length, nbytes);

    return nbytes;
}

static void
trace_input_eof(void *ctx)
{
    struct istream_trace *trace = ctx;

    fprintf(stderr, "%p eof()\n", (const void*)trace);

    trace->input = NULL;
    istream_deinit_eof(&trace->output);
}

static void
trace_input_abort(void *ctx)
{
    struct istream_trace *trace = ctx;

    fprintf(stderr, "%p abort()\n", (const void*)trace);

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
istream_trace_available(istream_t istream, bool partial)
{
    struct istream_trace *trace = istream_to_trace(istream);
    off_t available;

    fprintf(stderr, "%p available(%d)\n", (const void*)trace, partial);
    available = istream_available(trace->input, partial);
    fprintf(stderr, "%p available(%d)=%ld\n", (const void*)trace,
            partial, (long)available);

    return available;
}

static void
istream_trace_read(istream_t istream)
{
    struct istream_trace *trace = istream_to_trace(istream);

    fprintf(stderr, "%p read(0x%x)\n", (const void*)trace,
            trace->output.handler_direct);

    istream_handler_set_direct(trace->input, trace->output.handler_direct);
    istream_read(trace->input);
}

static void
istream_trace_close(istream_t istream)
{
    struct istream_trace *trace = istream_to_trace(istream);

    fprintf(stderr, "%p close()\n", (const void*)trace);

    istream_close_handler(trace->input);
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

    fprintf(stderr, "%p new()\n", (const void*)trace);

    istream_assign_handler(&trace->input, input,
                           &trace_input_handler, trace,
                           0);

    return istream_struct_cast(&trace->output);
}
