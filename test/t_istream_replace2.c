#include "istream.h"
#include "istream-replace.h"

#define EXPECTED_RESULT "abcfoofghijklmnopqrstuvwxyz"

static struct istream *
create_input(struct pool *pool)
{
    return istream_string_new(pool, "foo");
}

static struct istream *
create_test(struct pool *pool, struct istream *input)
{
    struct istream *istream =
        istream_string_new(pool, "abcdefghijklmnopqrstuvwxyz");
    istream = istream_replace_new(pool, istream);
    istream_replace_add(istream, 3, 3, input);
    istream_replace_extend(istream, 3, 4);
    istream_replace_extend(istream, 3, 5);
    istream_replace_finish(istream);
    return istream;
}

#include "t-istream-filter.h"
