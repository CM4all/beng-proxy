#include "istream.h"

#include <stdio.h>

static istream_t
create_input(pool_t pool)
{
    return istream_string_new(pool, "foo");
}

static istream_t
create_test(pool_t pool, istream_t input)
{
    return istream_subst_new(pool, input, "foo", "bar");
}

#include "t-istream-filter.h"
