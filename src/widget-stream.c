/*
 * Convert a widget to an istream.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget-stream.h"
#include "http-response.h"

#include <inline/compiler.h>

/*
 * HTTP response handler
 *
 */

static void
ws_response(__attr_unused http_status_t status,
            __attr_unused struct strmap *headers,
            istream_t body, void *ctx)
{
    struct widget_stream *ws = ctx;

    assert(ws->delayed != NULL);

    if (body == NULL)
        body = istream_null_new(ws->pool);

    istream_delayed_set(ws->delayed, body);

    if (istream_has_handler(ws->delayed))
        istream_read(ws->delayed);
}

static void
ws_abort(void *ctx)
{
    struct widget_stream *ws = ctx;
    istream_t delayed;

    assert(ws->delayed != NULL);

    delayed = ws->delayed;
    ws->delayed = NULL;

    /* clear the delayed async_ref object: we didn't provide an
       istream to the delayed object, and if we close it right now, it
       will trigger the async_abort(), unless we clear its
       async_ref */
    async_ref_clear(istream_delayed_async(delayed));

    istream_close(delayed);
}

const struct http_response_handler widget_stream_response_handler = {
    .response = ws_response,
    .abort = ws_abort,
};


/*
 * constructor
 *
 */

struct widget_stream *
widget_stream_new(pool_t pool)
{
    struct widget_stream *ws = p_malloc(pool, sizeof(*ws));

    ws->pool = pool;
    ws->delayed = istream_delayed_new(pool);
    return ws;
}
