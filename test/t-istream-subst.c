#include "istream.h"

#define EXPECTED_RESULT "bar fo fo bar bla!"

static istream_t
create_input(pool_t pool)
{
    return istream_string_new(pool, "foo fo fo bar blablablablubb");
}

static istream_t
create_test(pool_t pool, istream_t input)
{
    istream_t istream = istream_subst_new(pool, input);
    istream_subst_add(istream, "foo", "bar");
    istream_subst_add(istream, "blablablubb", "!");
    return istream;
}

#include "t-istream-filter.h"
