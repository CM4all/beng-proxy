/*
 * Serve HTTP requests from the local VFS.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "generate_response.hxx"
#include "request.hxx"
#include "http_headers.hxx"
#include "header_writer.hxx"
#include "GrowingBuffer.hxx"
#include "istream/istream_string.hxx"

#include <assert.h>

void
method_not_allowed(Request &request2, const char *allow)
{
    assert(allow != nullptr);

    HttpHeaders headers(request2.pool);
    headers.Write("content-type", "text/plain");
    headers.Write("allow", allow);

    response_dispatch(request2, HTTP_STATUS_METHOD_NOT_ALLOWED,
                      std::move(headers),
                      istream_string_new(&request2.pool,
                                         "This method is not allowed."));
}
