#include "google-gadget.h"
#include "widget.h"
#include "processor.h"
#include "url-stock.h"
#include "stock.h"
#include "uri.h"
#include "session.h"
#include "embed.h"

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


/*
 * main
 *
 */

int main(int argc, char **argv) {
    struct event_base *event_base;
    pool_t root_pool, pool;
    istream_t istream;
    const char *uri;
    int ret;
    struct parsed_uri parsed_uri;
    struct widget widget;
    struct processor_env env;

    event_base = event_init();

    if (argc != 2) {
        fprintf(stderr, "usage: %s URL\n", argv[0]);
        return 1;
    }

    root_pool = pool_new_libc(NULL, "root");

    pool = pool_new_linear(root_pool, "test", 8192);

    uri = "/beng.html";
    ret = uri_parse(pool, &parsed_uri, uri);
    if (ret != 0) {
        fprintf(stderr, "uri_parse() failed\n");
        exit(2);
    }

    widget_init(&widget, get_widget_class(pool, argv[1]));

    session_manager_init(pool);

    processor_env_init(pool, &env,
                       url_hstock_new(pool),
                       "localhost",
                       "http://localhost:8080/beng.html",
                       &parsed_uri,
                       NULL,
                       session_new(),
                       NULL,
                       NULL,
                       embed_widget_callback);

    widget_copy_from_request(&widget, &env);
    widget_determine_real_uri(pool, &widget);

    istream = embed_google_gadget(pool, &env, &widget);

    istream_handler_set(istream, &my_istream_handler, NULL, 0);

    pool_unref(pool);
    pool_commit();

    istream_read(istream);

    event_dispatch();

    session_manager_deinit();
    hstock_free(&env.http_client_stock);

    pool_unref(root_pool);
    pool_commit();

    pool_recycler_clear();

    event_base_free(event_base);
}
