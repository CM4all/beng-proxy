/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "sink_close.hxx"
#include "istream.h"

#include <stdlib.h>

static size_t
sink_close_data(gcc_unused const void *data, gcc_unused size_t length,
                void *ctx)
{
    auto *istream = (struct istream *)ctx;

    istream_close_handler(istream);
    return 0;
}

gcc_noreturn
static void
sink_close_eof(gcc_unused void *_ctx)
{
    /* should not be reachable, because we expect the istream to call
       the data() callback at least once */

    abort();
}

gcc_noreturn
static void
sink_close_abort(gcc_unused GError *error, gcc_unused void *_ctx)
{
    /* should not be reachable, because we expect the istream to call
       the data() callback at least once */

    abort();
}

static constexpr struct istream_handler sink_close_handler = {
    .data = sink_close_data,
    .eof = sink_close_eof,
    .abort = sink_close_abort,
};

void
sink_close_new(struct istream *istream)
{
    istream_handler_set(istream, &sink_close_handler, istream, 0);
}
