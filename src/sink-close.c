/*
 * An istream handler which closes the istream as soon as data
 * arrives.  This is used in the test cases.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "sink-impl.h"

#include <stdlib.h>

static size_t
sink_close_data(__attr_unused const void *data, __attr_unused size_t length,
                void *ctx)
{
    istream_t istream = ctx;

    istream_close_handler(istream);
    return 0;
}

__attr_noreturn
static void
sink_close_eof(__attr_unused void *_ctx)
{
    /* should not be reachable, because we expect the istream to call
       the data() callback at least once */

    abort();
}

static const struct istream_handler sink_close_handler = {
    .data = sink_close_data,
    .eof = sink_close_eof,
    .abort = sink_close_eof,
};

void
sink_close_new(istream_t istream)
{
    istream_handler_set(istream, &sink_close_handler, istream, 0);
}
