#include "sink-header.h"
#include "async.h"

#include <string.h>

#define EXPECTED_RESULT "foo"

static istream_t
create_input(pool_t pool)
{
    return istream_memory_new(pool, "\0\0\0\x06" "foobarfoo", 13);
}

static void
my_sink_header_done(void *header, size_t length, istream_t tail, void *ctx)
{
    istream_t delayed = ctx;

    assert(length == 6);
    assert(header != NULL);
    assert(memcmp(header, "foobar", 6) == 0);
    assert(tail != NULL);

    istream_delayed_set(delayed, tail);
    if (istream_has_handler(delayed))
        istream_read(delayed);
}

static void
my_sink_header_error(void *ctx)
{
    istream_t delayed = ctx;

    istream_delayed_set_abort(delayed);
}

static const struct sink_header_handler my_sink_header_handler = {
    .done = my_sink_header_done,
    .error = my_sink_header_error,
};

static istream_t
create_test(pool_t pool, istream_t input)
{
    istream_t delayed, hold;

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

#include "t-istream-filter.h"
