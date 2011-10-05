#include "istream.h"

#define EXPECTED_RESULT "foo"

static istream_t
create_input(struct pool *pool)
{
    return istream_string_new(pool, "foo");
}

static istream_t
create_test(struct pool *pool, istream_t input)
{
    return istream_pipe_new(pool, input, NULL);
}

#include "t-istream-filter.h"
