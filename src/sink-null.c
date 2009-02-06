/*
 * An istream handler which silently discards everything and ignores errors.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "sink-impl.h"

static size_t
sink_null_data(__attr_unused const void *data, size_t length,
               __attr_unused void *_ctx)
{
    return length;
}

static void
sink_null_eof(__attr_unused void *_ctx)
{
}

static const struct istream_handler sink_null_handler = {
    .data = sink_null_data,
    .eof = sink_null_eof,
    .abort = sink_null_eof,
};

void
sink_null_new(istream_t istream)
{
    istream_handler_set(istream, &sink_null_handler, NULL, 0);
}
