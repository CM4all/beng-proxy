#include "istream/istream_iconv.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream.hxx"

#define EXPECTED_RESULT "f\xc3\xbc\xc3\xbc"

class EventLoop;

static Istream *
create_input(struct pool *pool)
{
    return istream_string_new(pool, "f\xfc\xfc");
}

static Istream *
create_test(EventLoop &, struct pool *pool, Istream *input)
{
    return istream_iconv_new(pool, *input, "utf-8", "iso-8859-1");
}

#include "t_istream_filter.hxx"
