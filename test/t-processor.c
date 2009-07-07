#include "processor.h"
#include "uri-parser.h"
#include "embed.h"
#include "widget.h"
#include "widget-stream.h"
#include "rewrite-uri.h"

#include <event.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

static bool is_eof;


/*
 * emulate missing librarie
 *
 */

const struct widget_class root_widget_class = {
    .address = {
        .type = RESOURCE_ADDRESS_NONE,
    },
    .stateful = false,
};

struct tcache *global_translate_cache;

istream_t
embed_inline_widget(pool_t pool,
                    __attr_unused struct processor_env *env,
                    struct widget *widget)
{
    const char *s = widget_path(widget);
    if (s == NULL)
        s = "widget";

    return istream_string_new(pool, s);
}

void
embed_frame_widget(__attr_unused pool_t pool,
                   __attr_unused struct processor_env *env,
                   __attr_unused struct widget *widget,
                   const struct http_response_handler *handler,
                   void *handler_ctx,
                   __attr_unused struct async_operation_ref *async_ref)
{
    http_response_handler_direct_abort(handler, handler_ctx);
}

struct widget_session *
widget_get_session(__attr_unused struct widget *widget,
                   __attr_unused struct session *session,
                   __attr_unused bool create)
{
    return NULL;
}

istream_t
rewrite_widget_uri(__attr_unused pool_t pool, __attr_unused pool_t widget_pool,
                   __attr_unused struct tcache *translate_cache,
                   __attr_unused const char *partition_domain,
                   __attr_unused const struct parsed_uri *external_uri,
                   __attr_unused struct strmap *args,
                   __attr_unused struct widget *widget,
                   __attr_unused session_id_t session_id,
                   __attr_unused const struct strref *value,
                   __attr_unused enum uri_mode mode,
                   __attr_unused bool stateful)
{
    return NULL;
}


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

    widget_init(&widget, pool, &root_widget_class);

    processor_env_init(pool, &env,
                       NULL,
                       "localhost:8080",
                       "http://localhost:8080/beng.html",
                       &parsed_uri,
                       NULL,
                       0xdeadbeef,
                       HTTP_METHOD_GET, NULL,
                       NULL);

    ws = widget_stream_new(pool);
    delayed = ws->delayed;
    istream_handler_set(delayed, &my_istream_handler, NULL, 0);

    processor_new(pool, HTTP_STATUS_OK, NULL,
                  istream_file_new(pool, "/dev/stdin", (off_t)-1),
                  &widget, &env, PROCESSOR_CONTAINER,
                  &widget_stream_response_handler, ws,
                  widget_stream_async_ref(ws));

    if (!is_eof)
        event_dispatch();

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();

    event_base_free(event_base);
}
