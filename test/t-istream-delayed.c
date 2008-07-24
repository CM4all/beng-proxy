#include "istream.h"
#include "async.h"

#include <stdio.h>

#define EXPECTED_RESULT "foo"

static istream_t
create_input(pool_t pool)
{
    return istream_string_new(pool, "foo");
}

static void
my_delayed_abort(struct async_operation *ao)
{
    (void)ao;
    printf("delayed_abort\n");
}

static struct async_operation_class my_delayed_operation = {
    .abort = my_delayed_abort,
};

static istream_t
create_test(pool_t pool, istream_t input)
{
    istream_t istream;
    static struct async_operation async;

    async_init(&async, &my_delayed_operation);
    istream = istream_delayed_new(pool);
    async_ref_set(istream_delayed_async(istream), &async);

    istream_delayed_set(istream, input);
    return istream;
}

#include "t-istream-filter.h"
