#include "istream/istream_replace.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream.hxx"

#define EXPECTED_RESULT "foo"

static Istream *
create_input(struct pool *pool)
{
    return istream_string_new(pool, "foo");
}

static Istream *
create_test(struct pool *pool, Istream *input)
{
    Istream *istream = istream_replace_new(*pool, *input);
    istream_replace_add(*istream, 0, 0, nullptr);
    istream_replace_add(*istream, 3, 3, nullptr);
    istream_replace_finish(*istream);
    return istream;
}

#include "t_istream_filter.hxx"
