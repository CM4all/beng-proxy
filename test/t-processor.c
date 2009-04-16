#include "processor.h"
#include "uri-parser.h"
#include "embed.h"
#include "widget.h"
#include "session.h"
#include "widget-stream.h"

#include <event.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

static bool is_eof;

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
    fprintf(stderr, "in my_istream_eof()\n");
    session_manager_deinit();
    is_eof = true;
}

static void __attr_noreturn
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
    struct widget_stream *ws;
    istream_t delayed;

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

    session_manager_init();

    processor_env_init(pool, &env,
                       NULL,
                       "localhost:8080",
                       "http://localhost:8080/beng.html",
                       &parsed_uri,
                       NULL,
                       session_new()->uri_id,
                       NULL,
                       NULL);

    ws = widget_stream_new(pool);
    delayed = ws->delayed;

    processor_new(pool, NULL, istream_file_new(pool, "/dev/stdin", (off_t)-1),
                  &widget, &env, PROCESSOR_CONTAINER,
                  &widget_stream_response_handler, ws,
                  widget_stream_async_ref(ws));
                              
    istream_read(delayed);

    if (!is_eof)
        event_dispatch();

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();

    event_base_free(event_base);
}
