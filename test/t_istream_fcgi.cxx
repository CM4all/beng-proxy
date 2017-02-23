#include "istream/istream_string.hxx"
#include "fcgi/istream_fcgi.hxx"

class EventLoop;

static Istream *
create_input(struct pool *pool)
{
    return istream_string_new(pool, "foo");
}

static Istream *
create_test(EventLoop &, struct pool *pool, Istream *input)
{
    return istream_fcgi_new(*pool, *input, 1);
}

#include "t_istream_filter.hxx"
