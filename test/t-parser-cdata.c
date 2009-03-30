#include "parser.h"
#include "istream.h"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

static bool should_exit;

/*
 * parser handler
 *
 */

static void
parser_tag_start(const struct parser_tag *tag, void *ctx)
{
    (void)tag;
    (void)ctx;
}

static void
parser_tag_finished(const struct parser_tag *tag, void *ctx)
{
    (void)tag;
    (void)ctx;
}

static void
parser_attr_finished(const struct parser_attr *attr, void *ctx)
{
    (void)attr;
    (void)ctx;
}

static size_t
parser_cdata(const char *p, size_t length, bool escaped, void *ctx)
{
    (void)escaped;
    (void)ctx;

    write(1, p, length);
    return length;
}

static void
parser_eof(void *ctx, off_t length)
{
    (void)ctx;
    (void)length;

    should_exit = true;
}

static __attr_noreturn void
parser_abort(void *ctx)
{
    (void)ctx;

    fprintf(stderr, "ABORT\n");
    exit(2);
}

static const struct parser_handler my_parser_handler = {
    .tag_start = parser_tag_start,
    .tag_finished = parser_tag_finished,
    .attr_finished = parser_attr_finished,
    .cdata = parser_cdata,
    .eof = parser_eof,
    .abort = parser_abort,
};


/*
 * main
 *
 */

int main(int argc, char **argv) {
    pool_t root_pool, pool;
    istream_t istream;
    struct parser *parser;

    (void)argc;
    (void)argv;

    root_pool = pool_new_libc(NULL, "root");

    pool = pool_new_linear(root_pool, "test", 8192);

    istream = istream_file_new(pool, "/dev/stdin", (off_t)-1);
    parser = parser_new(pool, istream, &my_parser_handler, NULL);

    while (!should_exit)
        parser_read(parser);

    pool_unref(pool);
    pool_unref(root_pool);
    pool_commit();

    pool_recycler_clear();
}
