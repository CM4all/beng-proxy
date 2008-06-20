/*
 * Common HTTP stuff.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http.h"

const char *http_method_to_string_data[HTTP_METHOD_INVALID] = {
    [HTTP_METHOD_HEAD] = "HEAD",
    [HTTP_METHOD_GET] = "GET",
    [HTTP_METHOD_POST] = "POST",
    [HTTP_METHOD_PUT] = "PUT",
    [HTTP_METHOD_DELETE] = "DELETE",
};

const char *http_status_to_string_data[6][20] = {
    [2] = {
        [HTTP_STATUS_OK - 200] = "200 OK",
        [HTTP_STATUS_CREATED - 200] = "201 Created",
        [HTTP_STATUS_NO_CONTENT - 200] = "204 No Content",
        [HTTP_STATUS_PARTIAL_CONTENT - 200] = "206 Partial Content",
    },
    [3] = {
        [HTTP_STATUS_MOVED_PERMANENTLY - 300] = "301 Moved Permanently",
        [HTTP_STATUS_FOUND - 300] = "302 Found",
        [HTTP_STATUS_SEE_OTHER - 300] = "303 See Other",
        [HTTP_STATUS_NOT_MODIFIED - 300] = "304 Not Modified",
        [HTTP_STATUS_TEMPORARY_REDIRECT - 300] = "307 Temporary Redirect",
    },
    [4] = {
        [HTTP_STATUS_BAD_REQUEST - 400] = "400 Bad Request",
        [HTTP_STATUS_UNAUTHORIZED - 400] = "401 Unauthorized",
        [HTTP_STATUS_FORBIDDEN - 400] = "403 Forbidden",
        [HTTP_STATUS_NOT_FOUND - 400] = "404 Not Found",
        [HTTP_STATUS_METHOD_NOT_ALLOWED - 400] = "405 Method Not Allowed",
        [HTTP_STATUS_PRECONDITION_FAILED - 400] = "412 Precondition Failed",
        [HTTP_STATUS_REQUESTED_RANGE_NOT_SATISFIABLE - 400] = "416 Requested Range Not Satisfiable",
    },
    [5] = {
        [HTTP_STATUS_INTERNAL_SERVER_ERROR - 500] = "500 Internal Server Error",
        [HTTP_STATUS_NOT_IMPLEMENTED - 500] = "501 Not Implemented",
        [HTTP_STATUS_BAD_GATEWAY - 500] = "502 Bad Gateway",
        [HTTP_STATUS_SERVICE_UNAVAILABLE - 500] = "503 Service Unavailable",
        [HTTP_STATUS_GATEWAY_TIMEOUT - 500] = "504 Gateway Timeout",
        [HTTP_STATUS_HTTP_VERSION_NOT_SUPPORTED - 500] = "505 HTTP Version Not Supported",
    },
};
