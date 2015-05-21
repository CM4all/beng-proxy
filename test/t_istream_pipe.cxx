#include "istream.h"
#include "istream_pipe.hxx"
#include "istream_string.hxx"

#define EXPECTED_RESULT "foo"

static struct istream *
create_input(struct pool *pool)
{
    return istream_string_new(pool, "foo");
}

static struct istream *
create_test(struct pool *pool, struct istream *input)
{
    return istream_pipe_new(pool, input, NULL);
}

#include "t_istream_filter.hxx"
