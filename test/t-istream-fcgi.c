#include "istream.h"

static istream_t
create_input(struct pool *pool)
{
    return istream_string_new(pool, "foo");
}

static istream_t
create_test(struct pool *pool, istream_t input)
{
    return istream_fcgi_new(pool, input, 1);
}

#include "t-istream-filter.h"
