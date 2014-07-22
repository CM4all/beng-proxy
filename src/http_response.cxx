/*
 * Definition of the HTTP response handler.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http_response.hxx"
#include "strmap.hxx"
#include "istream.h"

void
http_response_handler::InvokeMessage(void *ctx, struct pool &pool,
                                     http_status_t status,
                                     const char *msg) const
{
    assert(http_status_is_valid(status));
    assert(msg != nullptr);

    struct strmap *headers = strmap_new(&pool);
    headers->Add("content-type", "text/plain; charset=utf-8");
    InvokeResponse(ctx, status, headers, istream_string_new(&pool, msg));
}
