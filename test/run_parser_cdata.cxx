#include "xml_parser.hxx"
#include "istream/istream.hxx"
#include "istream/istream_file.hxx"
#include "fb_pool.hxx"

#include <glib.h>

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

static bool should_exit;

/*
 * parser handler
 *
 */

static bool
parser_tag_start(const XmlParserTag *tag, void *ctx)
{
    (void)tag;
    (void)ctx;

    return false;
}

static void
parser_tag_finished(const XmlParserTag *tag, void *ctx)
{
    (void)tag;
    (void)ctx;
}

static void
parser_attr_finished(const XmlParserAttribute *attr, void *ctx)
{
    (void)attr;
    (void)ctx;
}

static size_t
parser_cdata(const char *p, size_t length, bool escaped,
             gcc_unused off_t start, void *ctx)
{
    (void)escaped;
    (void)ctx;

    (void)write(1, p, length);
    return length;
}

static void
parser_eof(void *ctx, off_t length)
{
    (void)ctx;
    (void)length;

    should_exit = true;
}

static gcc_noreturn void
parser_abort(GError *error, void *ctx)
{
    (void)ctx;

    fprintf(stderr, "ABORT: %s\n", error->message);
    g_error_free(error);
    exit(2);
}

static constexpr XmlParserHandler my_parser_handler = {
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
    struct pool *root_pool, *pool;
    struct istream *istream;

    (void)argc;
    (void)argv;

    fb_pool_init(false);

    root_pool = pool_new_libc(NULL, "root");

    pool = pool_new_linear(root_pool, "test", 8192);

    istream = istream_file_new(pool, "/dev/stdin", (off_t)-1, NULL);
    auto *parser = parser_new(*pool, istream, &my_parser_handler, NULL);

    while (!should_exit)
        parser_read(parser);

    pool_unref(pool);
    pool_unref(root_pool);
    pool_commit();

    pool_recycler_clear();

    fb_pool_deinit();
}
