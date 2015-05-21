#include "sink_header.hxx"
#include "async.hxx"
#include "istream.h"
#include "istream_hold.hxx"

#include <string.h>

#define EXPECTED_RESULT "foo"

static struct istream *
create_input(struct pool *pool)
{
    return istream_memory_new(pool, "\0\0\0\x06" "foobarfoo", 13);
}

static void
my_sink_header_done(void *header, size_t length, struct istream *tail,
                    void *ctx)
{
    istream *delayed = (istream *)ctx;

    assert(length == 6);
    assert(header != NULL);
    assert(memcmp(header, "foobar", 6) == 0);
    assert(tail != NULL);

    istream_delayed_set(delayed, tail);
    if (istream_has_handler(delayed))
        istream_read(delayed);
}

static void
my_sink_header_error(GError *error, void *ctx)
{
    istream *delayed = (istream *)ctx;

    istream_delayed_set_abort(delayed, error);
}

static const struct sink_header_handler my_sink_header_handler = {
    .done = my_sink_header_done,
    .error = my_sink_header_error,
};

static struct istream *
create_test(struct pool *pool, struct istream *input)
{
    struct istream *delayed, *hold;

    delayed = istream_delayed_new(pool);
    hold = istream_hold_new(pool, delayed);

    sink_header_new(pool, input,
                    &my_sink_header_handler, delayed,
                    istream_delayed_async_ref(delayed));

    istream_read(input);

    return hold;
}

#define NO_BLOCKING
#define NO_GOT_DATA_ASSERT

#include "t_istream_filter.hxx"
