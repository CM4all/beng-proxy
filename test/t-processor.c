#include "processor.h"
#include "uri.h"
#include "embed.h"
#include "widget.h"
#include "session.h"
#include "url-stock.h"

#include <event.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

static int should_exit;

/*
 * istream handler
 *
 */

static size_t
my_istream_data(const void *data, size_t length, void *ctx)
{
    ssize_t nbytes;

    (void)ctx;

    nbytes = write(1, data, length);
    if (nbytes < 0) {
        fprintf(stderr, "failed to write to stdout: %s\n",
                strerror(errno));
        exit(2);
    }

    if (nbytes == 0) {
        fprintf(stderr, "failed to write to stdout\n");
        exit(2);
    }

    return (size_t)nbytes;
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

int main(int argc, char **argv) {
    struct event_base *event_base;
    pool_t pool;
    const char *uri;
    int ret;
    struct parsed_uri parsed_uri;
    struct widget widget;
    struct processor_env env;
    istream_t processor;

    (void)argc;
    (void)argv;

    event_base = event_init();

    pool = pool_new_libc(NULL, "root");

    uri = "/beng.html";
    ret = uri_parse(pool, &parsed_uri, uri);
    if (ret != 0) {
        fprintf(stderr, "uri_parse() failed\n");
        exit(2);
    }

    widget_init(&widget, &root_widget_class);

    session_manager_init(pool);

    processor_env_init(pool, &env,
                       url_hstock_new(pool),
                       "localhost:8080",
                       "http://localhost:8080/beng.html",
                       &parsed_uri,
                       NULL,
                       session_new(),
                       NULL,
                       NULL,
                       embed_widget_callback);

    processor = processor_new(pool, istream_file_new(pool, "/dev/stdin", (off_t)-1),
                              &widget, &env, PROCESSOR_CONTAINER);
    istream_handler_set(processor, &my_istream_handler, NULL, 0);
                              
    istream_read(processor);

    event_dispatch();

    assert(should_exit);

    session_manager_deinit();

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();

    event_base_free(event_base);
}
