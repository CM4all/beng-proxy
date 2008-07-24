#include "istream.h"

#define EXPECTED_RESULT "abcfoodefghijklmnopqrstuvwxyz"

static istream_t
create_input(pool_t pool)
{
    return istream_string_new(pool, "foo");
}

static istream_t
create_test(pool_t pool, istream_t input)
{
    istream_t istream = istream_string_new(pool, "abcdefghijklmnopqrstuvwxyz");
    istream = istream_replace_new(pool, istream);
    istream_replace_add(istream, 3, 3, input);
    istream_replace_finish(istream);
    return istream;
}

#include "t-istream-filter.h"
