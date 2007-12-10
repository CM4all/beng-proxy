/*
 * Filter a resource through an HTTP server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "filter.h"
#include "url-stream.h"
#include "async.h"

#include <assert.h>

struct filter {
    struct async_operation_ref url_stream;
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
    url_stream_new(pool, http_client_stock,
                   HTTP_METHOD_POST, url,
                   headers, content_length, body,
                   handler, handler_ctx,
                   &filter->url_stream);

    /* XXX has the response_handler already been called? */
    return filter;
}

void
filter_close(filter_t filter)
{
    assert(filter != NULL);
    assert(async_ref_defined(&filter->url_stream));

    async_abort(&filter->url_stream);
}
