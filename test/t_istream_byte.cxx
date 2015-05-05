#include "istream_byte.hxx"
#include "istream.h"

#define EXPECTED_RESULT "foo"

static struct istream *
create_input(struct pool *pool)
{
    return istream_string_new(pool, "foo");
}

static struct istream *
create_test(struct pool *pool, struct istream *input)
{
    return istream_byte_new(pool, input);
}

#include "t_istream_filter.hxx"
