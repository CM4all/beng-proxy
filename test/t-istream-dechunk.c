#include "istream.h"

#include <stdio.h>

#define EXPECTED_RESULT "foo"

static istream_t
create_input(struct pool *pool)
{
    return istream_string_new(pool, "3\r\nfoo\r\n0\r\n\r\n");
}

static void
dechunk_eof(gcc_unused void *ctx)
{
}

static istream_t
create_test(struct pool *pool, istream_t input)
{
    return istream_dechunk_new(pool, input, dechunk_eof, NULL);
}

#include "t-istream-filter.h"
