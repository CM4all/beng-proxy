#include "istream.h"

#define EXPECTED_RESULT "test&lt;foo&amp;bar&gt;test&quot;test&apos;"

static istream_t
create_input(struct pool *pool)
{
    return istream_string_new(pool, "test<foo&bar>test\"test'");
}

static istream_t
create_test(struct pool *pool, istream_t input)
{
    return istream_html_escape_new(pool, input);
}

#include "t-istream-filter.h"
