#include "istream/istream_catch.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream.hxx"

#include <glib.h>

#include <stdio.h>

#define EXPECTED_RESULT "foo"

static Istream *
create_input(struct pool *pool)
{
    return istream_string_new(pool, "foo");
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
