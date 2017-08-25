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

#include "fcgi/Protocol.hxx"
#include "http/Method.h"
#include "http/Status.h"

#include <sys/types.h>
#include <string.h>

struct pool;
class StringMap;
struct fcgi_record_header;

struct FcgiRequest {
    uint16_t id;

    http_method_t method;
    const char *uri;
    StringMap *headers;

    off_t length;
};

void
read_fcgi_header(struct fcgi_record_header *header);

void
read_fcgi_request(struct pool *pool, FcgiRequest *r);

void
discard_fcgi_request_body(FcgiRequest *r);

void
write_fcgi_stdout(const FcgiRequest *r,
                  const void *data, size_t length);

static inline void
write_fcgi_stdout_string(const FcgiRequest *r,
                         const char *data)
{
    write_fcgi_stdout(r, data, strlen(data));
}

void
write_fcgi_headers(const FcgiRequest *r, http_status_t status,
                   StringMap *headers);

void
write_fcgi_end(const FcgiRequest *r);
