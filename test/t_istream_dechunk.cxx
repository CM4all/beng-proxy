#include "istream_dechunk.hxx"
#include "istream_string.hxx"
#include "istream.h"

#include <stdio.h>

#define EXPECTED_RESULT "foo"

static struct istream *
create_input(struct pool *pool)
{
    return istream_string_new(pool, "3\r\nfoo\r\n0\r\n\r\n");
}

static void
dechunk_eof(gcc_unused void *ctx)
{
}

static struct istream *
create_test(struct pool *pool, struct istream *input)
{
    return istream_dechunk_new(pool, input, dechunk_eof, nullptr);
}

#include "t_istream_filter.hxx"
