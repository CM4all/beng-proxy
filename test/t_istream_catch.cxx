#include "istream/istream_catch.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream.hxx"

#include <glib.h>

#include <stdio.h>

/* an input string longer than the "space" buffer (128 bytes) to
   trigger bugs due to truncated OnData() buffers */
#define EXPECTED_RESULT "long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long"

static Istream *
create_input(struct pool *pool)
{
    return istream_string_new(pool, EXPECTED_RESULT);
}

static GError *
catch_callback(GError *error, gcc_unused void *ctx)
{
    fprintf(stderr, "caught: %s\n", error->message);
    g_error_free(error);
    return nullptr;
}

static Istream *
create_test(struct pool *pool, Istream *input)
{
    return istream_catch_new(pool, *input, catch_callback, nullptr);
}

#define NO_AVAILABLE_CALL
#include "t_istream_filter.hxx"
