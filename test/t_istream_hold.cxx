#include "istream/istream_hold.hxx"
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
    return istream_hold_new(*pool, *input);
}

#include "t_istream_filter.hxx"
