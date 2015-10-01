#include "css_parser.hxx"
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

static void
my_parser_class_name(const CssParserValue *name, void *ctx)
{
    (void)ctx;

    printf(".%.*s\n", (int)name->value.length, name->value.data);
}

static void
my_parser_xml_id(const CssParserValue *id, void *ctx)
{
    (void)ctx;

    printf("#%.*s\n", (int)id->value.length, id->value.data);
}

static void
my_parser_property_keyword(const char *name, const char *value,
                           gcc_unused off_t start, gcc_unused off_t end,
                           void *ctx)
{
    (void)ctx;

    printf("%s = %s\n", name, value);
}

static void
my_parser_url(const CssParserValue *url, void *ctx)
{
    (void)ctx;

    printf("%.*s\n", (int)url->value.length, url->value.data);
}

static void
my_parser_import(const CssParserValue *url, void *ctx)
{
    (void)ctx;

    printf("import %.*s\n", (int)url->value.length, url->value.data);
}

static void
my_parser_eof(void *ctx, off_t length)
{
    (void)ctx;
    (void)length;

    should_exit = true;
}

static gcc_noreturn void
my_parser_error(GError *error, void *ctx)
{
    (void)ctx;

    fprintf(stderr, "ABORT: %s\n", error->message);
    g_error_free(error);
    exit(2);
}

static constexpr CssParserHandler my_parser_handler = {
    .class_name = my_parser_class_name,
    .xml_id = my_parser_xml_id,
    .property_keyword = my_parser_property_keyword,
    .url = my_parser_url,
    .import = my_parser_import,
    .eof = my_parser_eof,
    .error = my_parser_error,
};


/*
 * main
 *
 */

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    fb_pool_init(false);

    struct pool *root_pool = pool_new_libc(NULL, "root");
    struct pool *pool = pool_new_linear(root_pool, "test", 8192);

    struct istream *istream = istream_file_new(pool, "/dev/stdin", (off_t)-1,
                                               NULL);
    auto *parser =
        css_parser_new(pool, istream, false, &my_parser_handler, NULL);

    while (!should_exit)
        css_parser_read(parser);

    pool_unref(pool);
    pool_unref(root_pool);
    pool_commit();

    pool_recycler_clear();

    fb_pool_deinit();
}
