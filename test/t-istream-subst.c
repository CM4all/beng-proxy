#include "istream.h"

#define EXPECTED_RESULT "bar fo fo bar bla! fo"

static istream_t
create_input(struct pool *pool)
{
    return istream_string_new(pool, "foo fo fo bar blablablablubb fo");
}

static istream_t
create_test(struct pool *pool, istream_t input)
{
    istream_t istream = istream_subst_new(pool, input);
    istream_subst_add(istream, "foo", "bar");
    istream_subst_add(istream, "blablablubb", "!");
    return istream;
}

#include "t-istream-filter.h"
