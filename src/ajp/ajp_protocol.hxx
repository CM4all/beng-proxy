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

/*
 * Internal definitions and utilities for the AJPv13 protocol.
 */

#ifndef AJP_PROTOCOL_HXX
#define AJP_PROTOCOL_HXX

#include "util/Compiler.h"
#include <http/method.h>

#include <stdint.h>

enum ajp_method {
    AJP_METHOD_NULL = 0,
    AJP_METHOD_OPTIONS = 1,
    AJP_METHOD_GET = 2,
    AJP_METHOD_HEAD = 3,
    AJP_METHOD_POST = 4,
    AJP_METHOD_PUT = 5,
    AJP_METHOD_DELETE = 6,
    AJP_METHOD_TRACE = 7,
    AJP_METHOD_PROPFIND = 8,
    AJP_METHOD_PROPPATCH = 9,
    AJP_METHOD_MKCOL = 10,
    AJP_METHOD_COPY = 11,
    AJP_METHOD_MOVE = 12,
    AJP_METHOD_LOCK = 13,
    AJP_METHOD_UNLOCK = 14,
};

enum ajp_header_code {
    AJP_HEADER_NONE,
    AJP_HEADER_CODE_START = 0xa000,
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

enum ajp_response_header_code {
    AJP_RESPONSE_HEADER_NONE,
    AJP_RESPONSE_HEADER_CODE_START = 0xa000,
    AJP_RESPONSE_HEADER_CONTENT_TYPE = 0xa001,
    AJP_RESPONSE_HEADER_CONTENT_LANGUAGE = 0xa002,
    AJP_RESPONSE_HEADER_CONTENT_LENGTH = 0xa003,
    AJP_RESPONSE_HEADER_DATE = 0xa004,
    AJP_RESPONSE_HEADER_LAST_MODIFIED = 0xa005,
    AJP_RESPONSE_HEADER_LOCATION = 0xa006,
    AJP_RESPONSE_HEADER_SET_COOKIE = 0xa007,
    AJP_RESPONSE_HEADER_SET_COOKIE2 = 0xa008,
    AJP_RESPONSE_HEADER_SERVLET_ENGINE = 0xa009,
    AJP_RESPONSE_HEADER_STATUS = 0xa00a,
    AJP_RESPONSE_HEADER_WWW_AUTHENTICATE = 0xa00b,
};

enum ajp_attribute_code {
    AJP_ATTRIBUTE_QUERY_STRING = 0x05,
};

enum ajp_code {
    AJP_CODE_FORWARD_REQUEST = 2,
    AJP_CODE_SEND_BODY_CHUNK = 3,
    AJP_CODE_SEND_HEADERS = 4,
    AJP_CODE_END_RESPONSE = 5,
    AJP_CODE_GET_BODY_CHUNK = 6,
    AJP_CODE_SHUTDOWN = 7,
    AJP_CODE_CPONG_REPLY = 9,
    AJP_CODE_CPING = 10
};

struct ajp_header {
    uint8_t a, b;
    uint16_t length;
} gcc_packed;

struct ajp_send_body_chunk {
    uint8_t code;
    uint16_t length;
} gcc_packed;

struct ajp_get_body_chunk {
    uint8_t code;
    uint16_t length;
} gcc_packed;

gcc_const
enum ajp_method
to_ajp_method(http_method_t method);

gcc_pure
enum ajp_header_code
ajp_encode_header_name(const char *name);

gcc_pure
const char *
ajp_decode_header_name(enum ajp_header_code code);

gcc_pure
enum ajp_response_header_code
ajp_encode_response_header_name(const char *name);

gcc_pure
const char *
ajp_decode_response_header_name(enum ajp_response_header_code code);

#endif
