/*
 * Common HTTP stuff.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HTTP_H
#define __BENG_HTTP_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    HTTP_METHOD_NULL = 0,
    HTTP_METHOD_HEAD,
    HTTP_METHOD_GET,
    HTTP_METHOD_POST,
    HTTP_METHOD_PUT,
    HTTP_METHOD_DELETE,
    HTTP_METHOD_INVALID,
} http_method_t;

typedef enum {
    HTTP_STATUS_OK = 200,
    HTTP_STATUS_CREATED = 201,
    HTTP_STATUS_NO_CONTENT = 204,
    HTTP_STATUS_PARTIAL_CONTENT = 206,
    HTTP_STATUS_MOVED_PERMANENTLY = 301,
    HTTP_STATUS_FOUND = 302,
    HTTP_STATUS_SEE_OTHER = 303,
    HTTP_STATUS_NOT_MODIFIED = 304,
    HTTP_STATUS_TEMPORARY_REDIRECT = 307,
    HTTP_STATUS_BAD_REQUEST = 400,
    HTTP_STATUS_UNAUTHORIZED = 401,
    HTTP_STATUS_FORBIDDEN = 403,
    HTTP_STATUS_NOT_FOUND = 404,
    HTTP_STATUS_METHOD_NOT_ALLOWED = 405,
    HTTP_STATUS_PRECONDITION_FAILED = 412,
    HTTP_STATUS_REQUESTED_RANGE_NOT_SATISFIABLE = 416,
    HTTP_STATUS_INTERNAL_SERVER_ERROR = 500,
    HTTP_STATUS_NOT_IMPLEMENTED = 501,
    HTTP_STATUS_BAD_GATEWAY = 502,
    HTTP_STATUS_SERVICE_UNAVAILABLE = 503,
    HTTP_STATUS_GATEWAY_TIMEOUT = 504,
    HTTP_STATUS_HTTP_VERSION_NOT_SUPPORTED = 505,
} http_status_t;


extern const char *http_method_to_string_data[HTTP_METHOD_INVALID];

static inline bool
http_method_is_valid(http_method_t method)
{
    return method > HTTP_METHOD_NULL && method < HTTP_METHOD_INVALID;
}

static inline const char *
http_method_to_string(http_method_t method)
{
    assert(method < sizeof(http_method_to_string_data) / sizeof(http_method_to_string_data[0]));
    assert(http_method_to_string_data[method]);

    return http_method_to_string_data[method];
}


extern const char *http_status_to_string_data[6][20];

static inline bool
http_status_is_valid(http_status_t status)
{
    return (status / 100) < sizeof(http_status_to_string_data) /
        sizeof(http_status_to_string_data[0]) &&
        status % 100 < sizeof(http_status_to_string_data[0]) /
        sizeof(http_status_to_string_data[0][0]) &&
        http_status_to_string_data[status / 100][status % 100] != NULL;
}

static inline const char *
http_status_to_string(http_status_t status)
{
    assert(http_status_is_valid(status));

    return http_status_to_string_data[status / 100][status % 100];
}

static inline bool
http_status_is_success(http_status_t status)
{
    return status >= 200 && status < 300;
}

static inline bool
http_status_is_redirect(http_status_t status)
{
    return status >= 300 && status < 400;
}

static inline bool
http_status_is_empty(http_status_t status)
{
    return status == HTTP_STATUS_NO_CONTENT ||
        status == HTTP_STATUS_NOT_MODIFIED;
}

#endif
