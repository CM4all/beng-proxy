#include "istream/istream_catch.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream.hxx"
#include "util/Exception.hxx"

#include <stdio.h>

/* an input string longer than the "space" buffer (128 bytes) to
   trigger bugs due to truncated OnData() buffers */
#define EXPECTED_RESULT "long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long"

class EventLoop;

static Istream *
create_input(struct pool *pool)
{
    return istream_string_new(pool, EXPECTED_RESULT);
}

static std::exception_ptr 
catch_callback(std::exception_ptr ep, gcc_unused void *ctx)
{
    fprintf(stderr, "caught: %s\n", GetFullMessage(ep).c_str());
    return {};
}

static Istream *
create_test(EventLoop &, struct pool *pool, Istream *input)
{
    return istream_catch_new(pool, *input, catch_callback, nullptr);
}

#define NO_AVAILABLE_CALL
#include "t_istream_filter.hxx"
