/*
 * Filter a resource through an HTTP server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "filter.h"
#include "url-stream.h"

#include <assert.h>

struct filter {
    url_stream_t us;
};

filter_t attr_malloc
filter_new(pool_t pool,
           struct hstock *http_client_stock,
           const char *url,
           growing_buffer_t headers,
           off_t content_length, istream_t body,
           const struct http_response_handler *handler,
           void *handler_ctx)
{
    filter_t filter;

    assert(url != NULL);
    assert(handler != NULL);
    assert(handler->response != NULL);

    filter = p_malloc(pool, sizeof(*filter));
    filter->us = url_stream_new(pool, http_client_stock,
                                HTTP_METHOD_POST, url,
                                headers, content_length, body,
                                handler, handler_ctx);
    if (filter->us == NULL)
        return NULL;

    return filter;
}

void
filter_close(filter_t filter)
{
    url_stream_t us;

    assert(filter != NULL);
    assert(filter->us != NULL);

    us = filter->us;
    filter->us = NULL;

    url_stream_close(us);
}
