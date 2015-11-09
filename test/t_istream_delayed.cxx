#include "istream/istream_delayed.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream.hxx"
#include "async.hxx"

#include <stdio.h>

#define EXPECTED_RESULT "foo"

static Istream *
create_input(struct pool *pool)
{
    return istream_string_new(pool, "foo");
}

static void
my_delayed_abort(struct async_operation *ao)
{
    (void)ao;
    printf("delayed_abort\n");
}

static const struct async_operation_class my_delayed_operation = {
    .abort = my_delayed_abort,
};

static Istream *
create_test(struct pool *pool, Istream *input)
{
    Istream *istream;
    static struct async_operation async;

    async.Init(my_delayed_operation);
    istream = istream_delayed_new(pool);
    istream_delayed_async_ref(*istream)->Set(async);

    istream_delayed_set(*istream, *input);
    return istream;
}

#include "t_istream_filter.hxx"
