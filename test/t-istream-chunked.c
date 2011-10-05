#include "istream.h"

static istream_t
create_input(struct pool *pool)
{
    return istream_string_new(pool, "foo_bar_0123456789abcdefghijklmnopqrstuvwxyz");
}

static istream_t
create_test(struct pool *pool, istream_t input)
{
    return istream_chunked_new(pool, input);
}

#include "t-istream-filter.h"
