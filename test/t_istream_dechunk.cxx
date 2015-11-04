#include "istream/istream_dechunk.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream.hxx"

#include <stdio.h>

#define EXPECTED_RESULT "foo"

static Istream *
create_input(struct pool *pool)
{
    return istream_string_new(pool, "3\r\nfoo\r\n0\r\n\r\n ");
}

static void
dechunk_eof(gcc_unused void *ctx)
{
}

static Istream *
create_test(struct pool *pool, Istream *input)
{
    return istream_dechunk_new(pool, *input, dechunk_eof, nullptr);
}

#include "t_istream_filter.hxx"
