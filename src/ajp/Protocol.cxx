/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "Protocol.hxx"

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

    case HTTP_METHOD_REPORT:
    case HTTP_METHOD_PATCH:
        /* not supported by AJPv13 */
        return AJP_METHOD_NULL;

    case HTTP_METHOD_NULL:
    case HTTP_METHOD_INVALID:
        break;
    }

    return AJP_METHOD_NULL;
}

static constexpr struct {
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
    { AJP_HEADER_NONE, nullptr },
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

    return nullptr;
}

static constexpr struct {
    enum ajp_response_header_code code;
    const char *name;
} response_header_map[] = {
    { AJP_RESPONSE_HEADER_CONTENT_TYPE, "content-type" },
    { AJP_RESPONSE_HEADER_CONTENT_LANGUAGE, "content-language" },
    { AJP_RESPONSE_HEADER_CONTENT_LENGTH, "content-length" },
    { AJP_RESPONSE_HEADER_DATE, "date" },
    { AJP_RESPONSE_HEADER_LAST_MODIFIED, "last-modified" },
    { AJP_RESPONSE_HEADER_LOCATION, "location" },
    { AJP_RESPONSE_HEADER_SET_COOKIE, "set-cookie" },
    { AJP_RESPONSE_HEADER_SET_COOKIE2, "set-cookie2" },
    { AJP_RESPONSE_HEADER_SERVLET_ENGINE, "servlet-engine" },
    { AJP_RESPONSE_HEADER_STATUS, "status" },
    { AJP_RESPONSE_HEADER_WWW_AUTHENTICATE, "www-authenticate" },
    { AJP_RESPONSE_HEADER_NONE, nullptr },
};

enum ajp_response_header_code
ajp_encode_response_header_name(const char *name)
{
    for (unsigned i = 0;
         response_header_map[i].code != AJP_RESPONSE_HEADER_NONE; ++i)
        if (strcmp(response_header_map[i].name, name) == 0)
            return response_header_map[i].code;

    return AJP_RESPONSE_HEADER_NONE;
}

const char *
ajp_decode_response_header_name(enum ajp_response_header_code code)
{
    for (unsigned i = 0;
         response_header_map[i].code != AJP_RESPONSE_HEADER_NONE; ++i)
        if (response_header_map[i].code == code)
            return response_header_map[i].name;

    return nullptr;
}
