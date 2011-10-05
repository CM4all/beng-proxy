/*
 * An istream handler which silently discards everything and ignores errors.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "sink-impl.h"

static size_t
sink_null_data(gcc_unused const void *data, size_t length,
               gcc_unused void *_ctx)
{
    return length;
}

static void
sink_null_eof(gcc_unused void *_ctx)
{
}

static void
sink_null_abort(GError *error, gcc_unused void *_ctx)
{
    g_error_free(error);
}

static const struct istream_handler sink_null_handler = {
    .data = sink_null_data,
    .eof = sink_null_eof,
    .abort = sink_null_abort,
};

void
sink_null_new(istream_t istream)
{
    istream_handler_set(istream, &sink_null_handler, NULL, 0);
}
