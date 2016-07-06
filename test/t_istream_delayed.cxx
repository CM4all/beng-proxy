#include "istream/istream_delayed.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream.hxx"
#include "async.hxx"

#include <stdio.h>

#define EXPECTED_RESULT "foo"

class EventLoop;

struct DelayedTest {
    struct async_operation operation;

    DelayedTest() {
        operation.Init2<DelayedTest>();
    }

    void Abort() {
        printf("delayed_abort\n");
    }
};

static Istream *
create_input(struct pool *pool)
{
    return istream_string_new(pool, "foo");
}

static Istream *
create_test(EventLoop &, struct pool *pool, Istream *input)
{
    auto *test = NewFromPool<DelayedTest>(*pool);

    Istream *istream = istream_delayed_new(pool);
    istream_delayed_async_ref(*istream)->Set(test->operation);

    istream_delayed_set(*istream, *input);
    return istream;
}

#include "t_istream_filter.hxx"
