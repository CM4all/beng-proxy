/*
 * Serve HTTP requests from the local VFS.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "generate_response.h"
#include "request.h"
#include "http-server.h"
#include "header-writer.h"
#include "growing-buffer.h"
#include "istream.h"

#include <assert.h>

void
method_not_allowed(struct request *request2, const char *allow)
{
    struct http_server_request *request = request2->request;
    struct growing_buffer *headers = growing_buffer_new(request->pool, 128);

    assert(allow != NULL);

    header_write(headers, "content-type", "text/plain");
    header_write(headers, "allow", allow);

    response_dispatch(request2, HTTP_STATUS_METHOD_NOT_ALLOWED, headers,
                      istream_string_new(request->pool,
                                         "This method is not allowed."));
}
