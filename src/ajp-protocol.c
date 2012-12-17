/*
 * Internal definitions and utilities for the AJPv13 protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ajp-protocol.h"

#include <string.h>

enum ajp_method
to_ajp_method(http_method_t method)
{
    switch (method) {
    case HTTP_METHOD_HEAD:
        return AJP_METHOD_HEAD;

    case HTTP_METHOD_GET:
        return AJP_METHOD_GET;

    case HTTP_METHOD_POST:
        return AJP_METHOD_POST;

    case HTTP_METHOD_PUT:
        return AJP_METHOD_PUT;

    case HTTP_METHOD_DELETE:
        return AJP_METHOD_DELETE;

    case HTTP_METHOD_OPTIONS:
        return AJP_METHOD_OPTIONS;

    case HTTP_METHOD_TRACE:
        return AJP_METHOD_TRACE;

    case HTTP_METHOD_PROPFIND:
        return AJP_METHOD_PROPFIND;

    case HTTP_METHOD_PROPPATCH:
        return AJP_METHOD_PROPPATCH;

    case HTTP_METHOD_MKCOL:
        return AJP_METHOD_MKCOL;

    case HTTP_METHOD_COPY:
        return AJP_METHOD_COPY;

    case HTTP_METHOD_MOVE:
        return AJP_METHOD_MOVE;

    case HTTP_METHOD_LOCK:
        return AJP_METHOD_LOCK;

    case HTTP_METHOD_UNLOCK:
        return AJP_METHOD_UNLOCK;

    case HTTP_METHOD_NULL:
    case HTTP_METHOD_INVALID:
        break;
    }

    return AJP_METHOD_NULL;
}

static const struct {
    enum ajp_header_code code;
    const char *name;
} header_map[] = {
    { AJP_HEADER_ACCEPT, "accept" },
    { AJP_HEADER_ACCEPT_CHARSET, "accept-charset" },
    { AJP_HEADER_ACCEPT_ENCODING, "accept-encoding" },
    { AJP_HEADER_ACCEPT_LANGUAGE, "accept-language" },
    { AJP_HEADER_AUTHORIZATION, "authorization" },
    { AJP_HEADER_CONNECTION, "connection" },
    { AJP_HEADER_CONTENT_TYPE, "content-type" },
    { AJP_HEADER_CONTENT_LENGTH, "content-length" },
    { AJP_HEADER_COOKIE, "cookie" },
    { AJP_HEADER_COOKIE2, "cookie2" },
    { AJP_HEADER_HOST, "host" },
    { AJP_HEADER_PRAGMA, "pragma" },
    { AJP_HEADER_REFERER, "referer" },
    { AJP_HEADER_USER_AGENT, "user-agent" },
    { AJP_HEADER_NONE, NULL },
};

enum ajp_header_code
ajp_encode_header_name(const char *name)
{
    for (unsigned i = 0; header_map[i].code != AJP_HEADER_NONE; ++i)
        if (strcmp(header_map[i].name, name) == 0)
            return header_map[i].code;

    return AJP_HEADER_NONE;
}

const char *
ajp_decode_header_name(enum ajp_header_code code)
{
    for (unsigned i = 0; header_map[i].code != AJP_HEADER_NONE; ++i)
        if (header_map[i].code == code)
            return header_map[i].name;

    return NULL;
}
