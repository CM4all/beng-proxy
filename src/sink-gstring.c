#include "sink-gstring.h"

static size_t
sink_gstring_data(const void *data, size_t length, void *ctx)
{
    struct sink_gstring *sg = ctx;

    assert(!sg->finished);

    g_string_append_len(sg->value, data, length);
    return length;
}

static void
sink_gstring_eof(void *ctx)
{
    struct sink_gstring *sg = ctx;

    assert(!sg->finished);

    sg->finished = true;
}

static void
sink_gstring_abort(void *ctx)
{
    struct sink_gstring *sg = ctx;

    g_string_free(sg->value, true);
    sg->value = NULL;
    sg->finished = true;
}

static const struct istream_handler sink_gstring_handler = {
    .data = sink_gstring_data,
    .eof = sink_gstring_eof,
    .abort = sink_gstring_abort,
};

struct sink_gstring *
sink_gstring_new(pool_t pool, istream_t istream)
{
    struct sink_gstring *sg = p_malloc(pool, sizeof(*sg));

    sg->value = g_string_sized_new(256);
    sg->finished = false;

    istream_handler_set(istream, &sink_gstring_handler, sg, 0);
    return sg;
}
