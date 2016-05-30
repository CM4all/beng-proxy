#include "istream/istream_byte.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream.hxx"

#define EXPECTED_RESULT "foo"

class EventLoop;

static Istream *
create_input(struct pool *pool)
{
    return istream_string_new(pool, "foo");
}

static Istream *
create_test(EventLoop &, struct pool *pool, Istream *input)
{
    return istream_byte_new(*pool, *input);
}

#include "t_istream_filter.hxx"
