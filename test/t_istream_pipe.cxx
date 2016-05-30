#include "istream/istream.hxx"
#include "istream/istream_pipe.hxx"
#include "istream/istream_string.hxx"

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
    return istream_pipe_new(pool, *input, nullptr);
}

#include "t_istream_filter.hxx"
