#include "istream/istream_replace.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream.hxx"

#define EXPECTED_RESULT "abcfoofghijklmnopqrstuvwxyz"

class EventLoop;

static Istream *
create_input(struct pool *pool)
{
    return istream_string_new(pool, "foo");
}

static Istream *
create_test(EventLoop &, struct pool *pool, Istream *input)
{
    Istream *istream =
        istream_string_new(pool, "abcdefghijklmnopqrstuvwxyz");
    istream = istream_replace_new(*pool, *istream);
    istream_replace_add(*istream, 3, 3, input);
    istream_replace_extend(*istream, 3, 4);
    istream_replace_extend(*istream, 3, 5);
    istream_replace_finish(*istream);
    return istream;
}

#include "t_istream_filter.hxx"
