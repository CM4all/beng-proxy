#include "istream.hxx"
#include "istream_string.hxx"

static Istream *
create_input(struct pool *pool)
{
    return istream_string_new(pool, "foo");
}

static Istream *
create_test(struct pool *pool, Istream *input)
{
    return istream_fcgi_new(pool, input, 1);
}

#include "t_istream_filter.hxx"
