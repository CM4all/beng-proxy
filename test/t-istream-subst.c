#include "istream.h"

#define EXPECTED_RESULT "bar fo fo bar bla! fo"

static struct istream *
create_input(struct pool *pool)
{
    return istream_string_new(pool, "foo fo fo bar blablablablubb fo");
}

static struct istream *
create_test(struct pool *pool, struct istream *input)
{
    struct istream *istream = istream_subst_new(pool, input);
    istream_subst_add(istream, "foo", "bar");
    istream_subst_add(istream, "blablablubb", "!");
    return istream;
}

#include "t-istream-filter.h"
