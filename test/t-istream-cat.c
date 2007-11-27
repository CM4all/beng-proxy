#include "istream.h"

static istream_t
create_input(pool_t pool)
{
    return istream_string_new(pool, "foo");
}

static istream_t
create_test(pool_t pool, istream_t input)
{
    return istream_cat_new(pool, input, NULL);
}

#include "t-istream-filter.h"
