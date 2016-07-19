#include "istream/istream_delayed.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream.hxx"
#include "util/Cancellable.hxx"

#include <stdio.h>

#define EXPECTED_RESULT "foo"

class EventLoop;

struct DelayedTest final : Cancellable {
    /* virtual methods from class Cancellable */
    void Cancel() override {
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
    istream_delayed_cancellable_ptr(*istream) = *test;

    istream_delayed_set(*istream, *input);
    return istream;
}

#include "t_istream_filter.hxx"
