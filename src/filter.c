/*
 * Filter a resource through an HTTP server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "filter.h"
#include "url-stream.h"

void
filter_new(pool_t pool,
           struct hstock *http_client_stock,
           const char *url,
           growing_buffer_t headers,
           off_t content_length, istream_t body,
           const struct http_response_handler *handler,
           void *handler_ctx,
           struct async_operation_ref *async_ref)
{
    url_stream_new(pool, http_client_stock,
                   HTTP_METHOD_POST, url,
                   headers, content_length, body,
                   handler, handler_ctx,
                   async_ref);
}
