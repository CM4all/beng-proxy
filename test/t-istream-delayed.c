#include "istream.h"

#include <stdio.h>

static istream_t
create_input(pool_t pool)
{
    return istream_string_new(pool, "foo");
}

static void
my_abort_callback(void *ctx)
{
    (void)ctx;
    printf("delayed_abort\n");
}

static istream_t
create_test(pool_t pool, istream_t input)
{
    istream_t istream = istream_delayed_new(pool, my_abort_callback, NULL);
    istream_delayed_set(istream, input);
    return istream;
}

#include "t-istream-filter.h"
