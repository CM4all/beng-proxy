/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_trace.hxx"
#include "istream-internal.h"
#include "util/Cast.hxx"

#include <assert.h>
#include <stdio.h>

struct istream_trace {
    struct istream output;
    struct istream *input;
};

static void
trace_data(const void *data0, size_t length)
{
    const char *data = (const char *)data0;
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
    auto *trace = (struct istream_trace *)ctx;
    size_t nbytes;

    fprintf(stderr, "%p data(%zu)\n", (const void*)trace, length);
    trace_data(data, length);
    nbytes = istream_invoke_data(&trace->output, data, length);
    fprintf(stderr, "%p data(%zu)=%zu\n", (const void*)trace, length, nbytes);

    return nbytes;
}

static ssize_t
trace_input_direct(enum istream_direct type, int fd, size_t max_length,
                   void *ctx)
{
    auto *trace = (struct istream_trace *)ctx;

    fprintf(stderr, "%p direct(0x%x, %zd)\n", (const void*)trace,
            trace->output.handler_direct, max_length);
    auto nbytes = istream_invoke_direct(&trace->output, type, fd, max_length);
    fprintf(stderr, "%p direct(0x%x, %zd)=%zd\n", (const void*)trace,
            trace->output.handler_direct, max_length, nbytes);

    return nbytes;
}

static void
trace_input_eof(void *ctx)
{
    auto *trace = (struct istream_trace *)ctx;

    fprintf(stderr, "%p eof()\n", (const void*)trace);

    trace->input = nullptr;
    istream_deinit_eof(&trace->output);
}

static void
trace_input_abort(GError *error, void *ctx)
{
    auto *trace = (struct istream_trace *)ctx;

    fprintf(stderr, "%p abort('%s')\n", (const void*)trace, error->message);

    trace->input = nullptr;
    istream_deinit_abort(&trace->output, error);
}

static constexpr struct istream_handler trace_input_handler = {
    .data = trace_input_data,
    .direct = trace_input_direct,
    .eof = trace_input_eof,
    .abort = trace_input_abort,
};


/*
 * istream implementation
 *
 */

static inline constexpr struct istream_trace *
istream_to_trace(struct istream *istream)
{
    return &ContainerCast2(*istream, &istream_trace::output);
}

static off_t
istream_trace_available(struct istream *istream, bool partial)
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
istream_trace_read(struct istream *istream)
{
    struct istream_trace *trace = istream_to_trace(istream);

    fprintf(stderr, "%p read(0x%x)\n", (const void*)trace,
            trace->output.handler_direct);

    istream_handler_set_direct(trace->input, trace->output.handler_direct);
    istream_read(trace->input);
}

static void
istream_trace_close(struct istream *istream)
{
    struct istream_trace *trace = istream_to_trace(istream);

    fprintf(stderr, "%p close()\n", (const void*)trace);

    istream_close_handler(trace->input);
    istream_deinit(&trace->output);
}

static constexpr struct istream_class istream_trace = {
    .available = istream_trace_available,
    .read = istream_trace_read,
    .close = istream_trace_close,
};


/*
 * constructor
 *
 */

struct istream *
istream_trace_new(struct pool *pool, struct istream *input)
{
    struct istream_trace *trace = istream_new_macro(pool, trace);

    assert(input != nullptr);
    assert(!istream_has_handler(input));

    fprintf(stderr, "%p new()\n", (const void*)trace);

    istream_assign_handler(&trace->input, input,
                           &trace_input_handler, trace,
                           0);

    return istream_struct_cast(&trace->output);
}
