#include "istream/istream_deflate.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream.hxx"

static struct istream *
create_input(struct pool *pool)
{
    return istream_string_new(pool, "foo");
}

static struct istream *
create_test(struct pool *pool, struct istream *input)
{
    return istream_deflate_new(pool, input);
}

#include "t_istream_filter.hxx"
