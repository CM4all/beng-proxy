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

#include "file_headers.hxx"
#include "static_headers.hxx"
#include "GrowingBuffer.hxx"
#include "header_writer.hxx"
#include "request.hxx"
#include "http_server/Request.hxx"
#include "http_headers.hxx"
#include "translation/Vary.hxx"
#include "http/List.hxx"
#include "http/Date.hxx"
#include "util/DecimalFormat.h"

#include <attr/xattr.h>

#include <assert.h>
#include <stdlib.h>
#include <sys/stat.h>

gcc_pure
static std::chrono::seconds
read_xattr_max_age(int fd)
{
    assert(fd >= 0);

    char buffer[32];
    ssize_t nbytes = fgetxattr(fd, "user.MaxAge",
                               buffer, sizeof(buffer) - 1);
    if (nbytes <= 0)
        return std::chrono::seconds::zero();

    buffer[nbytes] = 0;

    char *endptr;
    unsigned long max_age = strtoul(buffer, &endptr, 10);
    if (*endptr != 0)
        return std::chrono::seconds::zero();

    return std::chrono::seconds(max_age);
}

static void
generate_expires(GrowingBuffer &headers,
                 std::chrono::system_clock::duration max_age)
{
    constexpr std::chrono::system_clock::duration max_max_age =
        std::chrono::hours(365 * 24);
    if (max_age > max_max_age)
        /* limit max_age to approximately one year */
        max_age = max_max_age;

    /* generate an "Expires" response header */
    header_write(headers, "expires",
                 http_date_format(std::chrono::system_clock::now() + max_age));
}

void
file_cache_headers(GrowingBuffer &headers,
                   int fd, const struct stat &st,
                   std::chrono::seconds max_age)
{
    assert(fd >= 0);

    char buffer[64];

    ssize_t nbytes;
    char etag[512];

    nbytes = fgetxattr(fd, "user.ETag",
                       etag + 1, sizeof(etag) - 3);
    if (nbytes > 0) {
        assert((size_t)nbytes < sizeof(etag));
        etag[0] = '"';
        etag[nbytes + 1] = '"';
        etag[nbytes + 2] = 0;
        header_write(headers, "etag", etag);
    } else {
        static_etag(buffer, st);
        header_write(headers, "etag", buffer);
    }

    if (max_age == std::chrono::seconds::zero())
        max_age = read_xattr_max_age(fd);

    if (max_age > std::chrono::seconds::zero())
        generate_expires(headers, max_age);
}

/**
 * Verifies the If-Range request header (RFC 2616 14.27).
 */
static bool
check_if_range(const char *if_range, const struct stat &st)
{
    if (if_range == nullptr)
        return true;

    const auto t = http_date_parse(if_range);
    if (t != std::chrono::system_clock::from_time_t(-1))
        return std::chrono::system_clock::from_time_t(st.st_mtime) == t;

    char etag[64];
    static_etag(etag, st);
    return strcmp(if_range, etag) == 0;
}

bool
file_evaluate_request(Request &request2,
                      int fd, const struct stat &st,
                      struct file_request &file_request)
{
    const auto &request = request2.request;
    const auto &request_headers = request.headers;
    const auto &tr = *request2.translate.response;
    bool ignore_if_modified_since = false;

    if (tr.status == 0 && request.method == HTTP_METHOD_GET &&
        !request2.IsTransformationEnabled()) {
        const char *p = request_headers.Get("range");

        if (p != nullptr &&
            check_if_range(request_headers.Get("if-range"), st))
            file_request.range.ParseRangeHeader(p);
    }

    if (!request2.IsTransformationEnabled()) {
        const char *p = request_headers.Get("if-match");
        if (p != nullptr && strcmp(p, "*") != 0) {
            char buffer[64];
            static_etag(buffer, st);

            if (!http_list_contains(p, buffer)) {
                response_dispatch(request2, HTTP_STATUS_PRECONDITION_FAILED,
                                  HttpHeaders(request2.pool), nullptr);
                return false;
            }
        }

        p = request_headers.Get("if-none-match");
        if (p != nullptr && strcmp(p, "*") == 0) {
            response_dispatch(request2, HTTP_STATUS_PRECONDITION_FAILED,
                              HttpHeaders(request2.pool), nullptr);
            return false;
        }

        if (p != nullptr) {
            char buffer[64];
            static_etag(buffer, st);

            if (http_list_contains(p, buffer)) {
                response_dispatch(request2, HTTP_STATUS_PRECONDITION_FAILED,
                                  HttpHeaders(request2.pool), nullptr);
                return false;
            }

            /* RFC 2616 14.26: "If none of the entity tags match, then
               the server MAY perform the requested method as if the
               If-None-Match header field did not exist, but MUST also
               ignore any If-Modified-Since header field(s) in the
               request." */
            ignore_if_modified_since = true;
        }
    }

    if (!request2.IsProcessorEnabled()) {
        const char *p = ignore_if_modified_since
            ? nullptr
            : request_headers.Get("if-modified-since");
        if (p != nullptr) {
            const auto t = http_date_parse(p);
            if (t != std::chrono::system_clock::from_time_t(-1) &&
                std::chrono::system_clock::from_time_t(st.st_mtime) <= t) {
                HttpHeaders headers(request2.pool);
                GrowingBuffer headers2 = headers.MakeBuffer();

                if (fd >= 0)
                    file_cache_headers(headers2, fd, st,
                                       tr.expires_relative);

                write_translation_vary_header(headers2, tr);

                response_dispatch(request2, HTTP_STATUS_NOT_MODIFIED,
                                  std::move(headers), nullptr);
                return false;
            }
        }

        p = request_headers.Get("if-unmodified-since");
        if (p != nullptr) {
            const auto t = http_date_parse(p);
            if (t != std::chrono::system_clock::from_time_t(-1) &&
                std::chrono::system_clock::from_time_t(st.st_mtime) > t) {
                response_dispatch(request2, HTTP_STATUS_PRECONDITION_FAILED,
                                  HttpHeaders(request2.pool), nullptr);
                return false;
            }
        }
    }

    return true;
}

void
file_response_headers(GrowingBuffer &headers,
                      const char *override_content_type,
                      int fd, const struct stat &st,
                      std::chrono::seconds expires_relative,
                      bool processor_enabled, bool processor_first)
{
    if (!processor_first && fd >= 0)
        file_cache_headers(headers, fd, st, expires_relative);
    else {
        char etag[64];
        static_etag(etag, st);
        header_write(headers, "etag", etag);

        if (expires_relative > std::chrono::seconds::zero())
            generate_expires(headers, expires_relative);
    }

    if (override_content_type != nullptr) {
        /* content type override from the translation server */
        header_write(headers, "content-type", override_content_type);
    } else {
        char content_type[256];
        if (load_xattr_content_type(content_type, sizeof(content_type), fd)) {
            header_write(headers, "content-type", content_type);
        } else {
            header_write(headers, "content-type", "application/octet-stream");
        }
    }

#ifndef NO_LAST_MODIFIED_HEADER
    if (!processor_enabled)
        header_write(headers, "last-modified",
                     http_date_format(std::chrono::system_clock::from_time_t(st.st_mtime)));
#endif
}
