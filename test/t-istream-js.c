#include "istream.h"
#include "js-filter.h"

static istream_t
create_input(pool_t pool)
{
    return istream_string_new(pool, "foo");
}

static istream_t
create_test(pool_t pool, istream_t input)
{
    return js_filter_new(pool, input);
}

#include "t-istream-filter.h"
