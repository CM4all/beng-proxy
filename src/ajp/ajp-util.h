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
 * Internal utilities for the AJPv13 client implementation.
 */

#ifndef __BENG_AJP_UTIL_H
#define __BENG_AJP_UTIL_H

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

static ajp_method_t
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

#endif
