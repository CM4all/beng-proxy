/*
 * Serve HTTP requests from the local VFS.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "generate_response.hxx"
#include "request.hxx"
#include "http_server.hxx"
#include "http_headers.hxx"
#include "header_writer.hxx"
#include "growing_buffer.hxx"
#include "istream_string.hxx"

#include <assert.h>

void
method_not_allowed(struct request &request2, const char *allow)
{
    struct http_server_request *request = request2.request;

    assert(allow != nullptr);

    HttpHeaders headers;
    headers.Write(*request->pool, "content-type", "text/plain");
    headers.Write(*request->pool, "allow", allow);

    response_dispatch(request2, HTTP_STATUS_METHOD_NOT_ALLOWED,
                      std::move(headers),
                      istream_string_new(request->pool,
                                         "This method is not allowed."));
}
