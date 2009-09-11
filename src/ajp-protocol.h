/*
 * Internal definitions and utilities for the AJPv13 protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_AJP_PROTOCOL_H
#define __BENG_AJP_PROTOCOL_H

#include "http.h"

#include <inline/compiler.h>

#include <stdint.h>

typedef enum {
    AJP_PREFIX_FORWARD_REQUEST = 0x02,
} ajp_prefix_code_t;

typedef enum {
    AJP_METHOD_NULL = 0,
    AJP_METHOD_OPTIONS = 1,
    AJP_METHOD_GET = 2,
    AJP_METHOD_HEAD = 3,
    AJP_METHOD_POST = 4,
    AJP_METHOD_PUT = 5,
    AJP_METHOD_DELETE = 6,
    AJP_METHOD_TRACE = 7,
} ajp_method_t;

enum ajp_header_code {
    AJP_HEADER_NONE,
    AJP_HEADER_ACCEPT = 0xa001,
    AJP_HEADER_ACCEPT_CHARSET = 0xa002,
    AJP_HEADER_ACCEPT_ENCODING = 0xa003,
    AJP_HEADER_ACCEPT_LANGUAGE = 0xa004,
    AJP_HEADER_AUTHORIZATION = 0xa005,
    AJP_HEADER_CONNECTION = 0xa006,
    AJP_HEADER_CONTENT_TYPE = 0xa007,
    AJP_HEADER_CONTENT_LENGTH = 0xa008,
    AJP_HEADER_COOKIE = 0xa009,
    AJP_HEADER_COOKIE2 = 0xa00a,
    AJP_HEADER_HOST = 0xa00b,
    AJP_HEADER_PRAGMA = 0xa00c,
    AJP_HEADER_REFERER = 0xa00d,
    AJP_HEADER_USER_AGENT = 0xa00e,
};

typedef enum {
    AJP_CODE_FORWARD_REQUEST = 2,
    AJP_CODE_SEND_BODY_CHUNK = 3,
    AJP_CODE_SEND_HEADERS = 4,
    AJP_CODE_END_RESPONSE = 5,
    AJP_CODE_GET_BODY_CHUNK = 6,
    AJP_CODE_SHUTDOWN = 7,
    AJP_CODE_CPONG_REPLY = 9,
    AJP_CODE_CPING = 10
} ajp_code_t;

struct ajp_header {
    uint8_t a, b;
    uint16_t length;
} __attr_packed;

struct ajp_send_body_chunk {
    uint8_t code;
    uint16_t length;
} __attr_packed;

struct ajp_get_body_chunk {
    uint8_t code;
    uint16_t length;
} __attr_packed;

static inline ajp_method_t
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

    case HTTP_METHOD_NULL:
    case HTTP_METHOD_INVALID:
        break;
    }

    return AJP_METHOD_NULL;
}

enum ajp_header_code
ajp_encode_header_name(const char *name);

const char *
ajp_decode_header_name(enum ajp_header_code code);

#endif
