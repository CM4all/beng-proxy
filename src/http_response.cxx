/*
 * Definition of the HTTP response handler.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http_response.hxx"
#include "strmap.hxx"
#include "istream/istream_string.hxx"

void
HttpResponseHandler::InvokeResponse(struct pool &pool,
                                    http_status_t status,
                                    const char *msg)
{
    assert(http_status_is_valid(status));
    assert(msg != nullptr);

    StringMap headers(pool);
    headers.Add("content-type", "text/plain; charset=utf-8");
    InvokeResponse(status, std::move(headers),
                   istream_string_new(&pool, msg));
}
