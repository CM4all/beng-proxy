#include "istream.h"

#include <stdio.h>

static void
my_dechunk_eof_callback(void *ctx)
{
    (void)ctx;
    printf("dechunk_eof\n");
}

static istream_t
create_input(pool_t pool)
{
    return istream_string_new(pool, "3\r\nfoo\r\n0\r\n");
}

static istream_t
create_test(pool_t pool, istream_t input)
{
    return istream_dechunk_new(pool, input,
                               my_dechunk_eof_callback, NULL);
}

#include "t-istream-filter.h"
