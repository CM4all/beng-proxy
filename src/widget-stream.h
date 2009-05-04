/*
 * Convert a widget to an istream.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_WIDGET_STREAM_H
#define __BENG_WIDGET_STREAM_H

#include "istream.h"
#include "async.h"

extern const struct http_response_handler widget_stream_response_handler;

struct widget_stream {
    pool_t pool;

    istream_t delayed;
};

struct widget_stream *
widget_stream_new(pool_t pool);

static inline struct async_operation_ref *
widget_stream_async_ref(struct widget_stream *ws)
{
    return istream_delayed_async_ref(ws->delayed);
}

#endif
