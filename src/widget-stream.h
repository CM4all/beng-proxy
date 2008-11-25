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
    istream_t delayed;
    struct async_operation_ref async_ref;
    struct async_operation async;
};

struct widget_stream *
widget_stream_new(pool_t pool);

static inline struct async_operation_ref *
widget_stream_async_ref(struct widget_stream *ws)
{
    return &ws->async_ref;
}

#endif
