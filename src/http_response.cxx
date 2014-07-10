/*
 * Definition of the HTTP response handler.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http_response.hxx"
#include "strmap.hxx"
#include "istream.h"

void
http_response_handler_direct_message(const struct http_response_handler *handler,
                                     void *ctx,
                                     struct pool *pool,
                                     http_status_t status, const char *msg)
{
    struct strmap *headers = strmap_new(pool);
    headers->Add("content-type", "text/plain; charset=utf-8");
    http_response_handler_direct_response(handler, ctx, status, headers,
                                          istream_string_new(pool, msg));
}

void
http_response_handler_invoke_message(struct http_response_handler_ref *ref,
                                     struct pool *pool,
                                     http_status_t status, const char *msg)
{
    struct strmap *headers = strmap_new(pool);
    headers->Add("content-type", "text/plain; charset=utf-8");
    http_response_handler_invoke_response(ref, status, headers,
                                          istream_string_new(pool, msg));
}
