#include "istream.h"

static istream_t
create_input(pool_t pool)
{
    return istream_string_new(pool, "foo_bar_0123456789abcdefghijklmnopqrstuvwxyz");
}

static istream_t
create_test(pool_t pool, istream_t input)
{
    return istream_chunked_new(pool, input);
}

#include "t-istream-filter.h"
