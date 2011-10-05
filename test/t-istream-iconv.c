#include "istream.h"

#define EXPECTED_RESULT "f\xc3\xbc\xc3\xbc"

static istream_t
create_input(struct pool *pool)
{
    return istream_string_new(pool, "f\xfc\xfc");
}

static istream_t
create_test(struct pool *pool, istream_t input)
{
    return istream_iconv_new(pool, input, "utf-8", "iso-8859-1");
}

#include "t-istream-filter.h"
