/*
 * Definition of the HTTP response handler.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-response.h"
#include "strmap.h"

void
http_response_handler_direct_message(const struct http_response_handler *handler,
                                     void *ctx,
                                     pool_t pool,
                                     http_status_t status, const char *msg)
{
    struct strmap *headers = strmap_new(pool, 2);
    strmap_add(headers, "content-type", "text/plain; charset=utf-8");
    http_response_handler_direct_response(handler, ctx, status, headers,
                                          istream_string_new(pool, msg));
}

void
http_response_handler_invoke_message(struct http_response_handler_ref *ref,
                                     pool_t pool,
                                     http_status_t status, const char *msg)
{
    struct strmap *headers = strmap_new(pool, 2);
    strmap_add(headers, "content-type", "text/plain; charset=utf-8");
    http_response_handler_invoke_response(ref, status, headers,
                                          istream_string_new(pool, msg));
}
