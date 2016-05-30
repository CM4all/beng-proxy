#include "istream_html_escape.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream.hxx"

#define EXPECTED_RESULT "test&lt;foo&amp;bar&gt;test&quot;test&apos;"

class EventLoop;

static Istream *
create_input(struct pool *pool)
{
    return istream_string_new(pool, "test<foo&bar>test\"test'");
}

static Istream *
create_test(EventLoop &, struct pool *pool, Istream *input)
{
    return istream_html_escape_new(*pool, *input);
}

#include "t_istream_filter.hxx"
