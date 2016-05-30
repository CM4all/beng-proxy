#include "istream/istream_subst.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream.hxx"

#define EXPECTED_RESULT "bar fo fo bar bla! fo"

class EventLoop;

static Istream *
create_input(struct pool *pool)
{
    return istream_string_new(pool, "foo fo fo bar blablablablubb fo");
}

static Istream *
create_test(EventLoop &, struct pool *pool, Istream *input)
{
    Istream *istream = istream_subst_new(pool, *input);
    istream_subst_add(*istream, "foo", "bar");
    istream_subst_add(*istream, "blablablubb", "!");
    return istream;
}

#include "t_istream_filter.hxx"
