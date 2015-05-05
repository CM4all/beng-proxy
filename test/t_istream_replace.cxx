#include "istream.h"
#include "istream_replace.hxx"

#define EXPECTED_RESULT "foo"

static struct istream *
create_input(struct pool *pool)
{
    return istream_string_new(pool, "foo");
}

static struct istream *
create_test(struct pool *pool, struct istream *input)
{
    struct istream *istream = istream_replace_new(pool, input);
    istream_replace_add(istream, 0, 0, nullptr);
    istream_replace_add(istream, 3, 3, nullptr);
    istream_replace_finish(istream);
    return istream;
}

#include "t_istream_filter.hxx"
