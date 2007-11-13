#include "parser.h"
#include "istream.h"

#include <unistd.h>
#include <stdlib.h>

static int should_exit;

/*
 * istream handler
 *
 */

static size_t
my_istream_data(const void *data, size_t length, void *ctx)
{
    struct parser *parser = ctx;

    parser_feed(parser, data, length);

    return length;
}

static void
my_istream_eof(void *ctx)
{
    (void)ctx;
    should_exit = 1;
}

static void attr_noreturn
my_istream_abort(void *ctx)
{
    (void)ctx;
    exit(2);
}

static const struct istream_handler my_istream_handler = {
    .data = my_istream_data,
    .eof = my_istream_eof,
    .abort = my_istream_abort,
};


/*
 * parser handler
 *
 */

void
parser_element_start(struct parser *parser)
{
    (void)parser;
}

void
parser_element_finished(struct parser *parser, off_t end)
{
    (void)parser;
    (void)end;
}

void
parser_attr_finished(struct parser *parser)
{
    (void)parser;
}

void
parser_cdata(struct parser *parser, const char *p, size_t length, int escaped)
{
    (void)parser;
    (void)escaped;

    write(1, p, length);
}


/*
 * main
 *
 */

int main(int argc, char **argv) {
    struct parser parser;
    pool_t root_pool, pool;
    istream_t istream;

    (void)argc;
    (void)argv;

    parser_init(&parser);

    root_pool = pool_new_libc(NULL, "root");

    pool = pool_new_linear(root_pool, "test", 8192);

    istream = istream_file_new(pool, "/dev/stdin", (off_t)-1);
    istream_handler_set(istream, &my_istream_handler, &parser, 0);

    while (!should_exit)
        istream_read(istream);

    pool_unref(pool);
    pool_unref(root_pool);
    pool_commit();

    pool_recycler_clear();
}
