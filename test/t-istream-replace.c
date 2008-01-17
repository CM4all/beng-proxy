#include "istream.h"

static istream_t
create_input(pool_t pool)
{
    return istream_string_new(pool, "foo");
}

static istream_t
create_test(pool_t pool, istream_t input)
{
    istream_t istream = istream_replace_new(pool, input, 0);
    istream_replace_add(istream, 0, 0, NULL);
    istream_replace_finish(istream);
    return istream;
}

#include "t-istream-filter.h"
