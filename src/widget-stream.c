/*
 * Convert a widget to an istream.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget-stream.h"
#include "http-response.h"

/*
 * HTTP response handler
 *
 */

static void
ws_response(http_status_t status, struct strmap *headers,
            istream_t body, void *ctx)
{
    struct widget_stream *ws = ctx;
    istream_t delayed;

    (void)status;
    (void)headers;

    if (ws->delayed == NULL)
        return;

    delayed = ws->delayed;
    ws->delayed = NULL;

    if (body == NULL)
        istream_delayed_set_eof(delayed);
    else {
        istream_delayed_set(delayed, body);

        if (istream_has_handler(delayed))
            istream_read(delayed);
    }
}

static void
ws_abort(void *ctx)
{
    struct widget_stream *ws = ctx;
    istream_t delayed;

    if (ws->delayed == NULL)
        return;

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
 * async operation
 *
 */

static struct widget_stream *
async_to_ws(struct async_operation *ao)
{
    return (struct widget_stream*)(((char*)ao) - offsetof(struct widget_stream, async));
}

static void
ws_delayed_abort(struct async_operation *ao)
{
    struct widget_stream *ws = async_to_ws(ao);

    assert(ws->delayed != NULL);

    ws->delayed = NULL;
    async_abort(&ws->async_ref);
}

static const struct async_operation_class ws_delayed_operation = {
    .abort = ws_delayed_abort,
};


/*
 * constructor
 *
 */

struct widget_stream *
widget_stream_new(pool_t pool)
{
    struct widget_stream *ws = p_malloc(pool, sizeof(*ws));

    ws->delayed = istream_delayed_new(pool);

    async_init(&ws->async, &ws_delayed_operation);
    async_ref_set(istream_delayed_async(ws->delayed), &ws->async);

    return ws;
}
