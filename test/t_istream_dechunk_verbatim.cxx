#include "istream_dechunk.hxx"
#include "istream_byte.hxx"
#include "istream_four.hxx"
#include "istream_string.hxx"
#include "istream.h"

#include <stdio.h>

#define EXPECTED_RESULT "3\r\nfoo\r\n0\r\n\r\n"

static struct istream *
create_input(struct pool *pool)
{
    return istream_string_new(pool, EXPECTED_RESULT);
}

static void
dechunk_eof(gcc_unused void *ctx)
{
}

static struct istream *
create_test(struct pool *pool, struct istream *input)
{
    input = istream_dechunk_new(pool, input, dechunk_eof, nullptr);
    istream_dechunk_check_verbatim(input);
#ifdef T_BYTE
    input = istream_byte_new(pool, input);
#endif
#ifdef T_FOUR
    input = istream_four_new(pool, input);
#endif
    return input;
}

#include "t_istream_filter.hxx"
