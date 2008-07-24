#include "istream.h"

static istream_t
create_input(pool_t pool)
{
    return istream_string_new(pool, "test<foo&bar>test");
}

static istream_t
create_test(pool_t pool, istream_t input)
{
    return istream_html_escape_new(pool, input);
}

#include "t-istream-filter.h"
