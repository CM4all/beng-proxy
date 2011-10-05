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
    istream_t istream = istream_replace_new(pool, input);
    istream_replace_add(istream, 0, 0, NULL);
    istream_replace_add(istream, 3, 3, NULL);
    istream_replace_finish(istream);
    return istream;
}

#include "t-istream-filter.h"
