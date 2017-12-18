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

#include "FileHandler.hxx"
#include "FileHeaders.hxx"
#include "file_address.hxx"
#include "Request.hxx"
#include "Instance.hxx"
#include "GenerateResponse.hxx"
#include "header_writer.hxx"
#include "http/Date.hxx"
#include "http_util.hxx"
#include "http_headers.hxx"
#include "http_server/Request.hxx"
#include "istream/istream_file.hxx"
#include "istream/istream.hxx"
#include "translation/Vary.hxx"
#include "util/DecimalFormat.h"

#include <assert.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdlib.h>

void
file_dispatch(Request &request2, const struct stat &st,
              const struct file_request &file_request,
              Istream *body)
{
    const TranslateResponse &tr = *request2.translate.response;
    const auto &address = *request2.handler.file.address;

    const char *override_content_type = request2.translate.content_type;
    if (override_content_type == nullptr)
        override_content_type = address.content_type;

    HttpHeaders headers(request2.pool);
    GrowingBuffer &headers2 = headers.GetBuffer();
    file_response_headers(headers2, override_content_type,
                          istream_file_fd(*body), st,
                          tr.expires_relative,
                          request2.IsProcessorFirst());
    write_translation_vary_header(headers2, tr);

    http_status_t status = tr.status == 0 ? HTTP_STATUS_OK : tr.status;

    /* generate the Content-Range header */

    header_write(headers2, "accept-ranges", "bytes");

    switch (file_request.range.type) {
    case HttpRangeRequest::Type::NONE:
        break;

    case HttpRangeRequest::Type::VALID:
        istream_file_set_range(*body, file_request.range.skip,
                               file_request.range.size);

        assert(body->GetAvailable(false) ==
               off_t(file_request.range.size - file_request.range.skip));

        status = HTTP_STATUS_PARTIAL_CONTENT;

        header_write(headers2, "content-range",
                     p_sprintf(&request2.pool, "bytes %lu-%lu/%lu",
                               (unsigned long)file_request.range.skip,
                               (unsigned long)(file_request.range.size - 1),
                               (unsigned long)st.st_size));
        break;

    case HttpRangeRequest::Type::INVALID:
        status = HTTP_STATUS_REQUESTED_RANGE_NOT_SATISFIABLE;

        header_write(headers2, "content-range",
                     p_sprintf(&request2.pool, "bytes */%lu",
                               (unsigned long)st.st_size));

        body->CloseUnused();
        body = nullptr;
        break;
    }

    /* finished, dispatch this response */

    request2.DispatchResponse(status, std::move(headers),
                              UnusedIstreamPtr(body));
}

static bool
file_dispatch_compressed(Request &request2, const struct stat &st,
                         Istream &body, const char *encoding,
                         const char *path)
{
    const TranslateResponse &tr = *request2.translate.response;
    const auto &address = *request2.handler.file.address;

    /* open compressed file */

    struct stat st2;
    UnusedIstreamPtr compressed_body;

    try {
        compressed_body = UnusedIstreamPtr(istream_file_stat_new(request2.instance.event_loop, request2.pool,
                                                                 path, st2));
    } catch (...) {
        return false;
    }

    if (!S_ISREG(st2.st_mode))
        return false;

    /* response headers with information from uncompressed file */

    const char *override_content_type = request2.translate.content_type;
    if (override_content_type == nullptr)
        override_content_type = address.content_type;

    HttpHeaders headers(request2.pool);
    GrowingBuffer &headers2 = headers.GetBuffer();
    file_response_headers(headers2, override_content_type,
                          istream_file_fd(body), st,
                          tr.expires_relative,
                          request2.IsProcessorFirst());
    write_translation_vary_header(headers2, tr);

    header_write(headers2, "content-encoding", encoding);
    header_write(headers2, "vary", "accept-encoding");

    /* close original file */

    body.CloseUnused();

    /* finished, dispatch this response */

    request2.compressed = true;

    http_status_t status = tr.status == 0 ? HTTP_STATUS_OK : tr.status;
    request2.DispatchResponse(status, std::move(headers),
                              std::move(compressed_body));
    return true;
}

static bool
file_check_compressed(Request &request2, const struct stat &st,
                      Istream &body, const char *encoding,
                      const char *path)
{
    const auto &request = request2.request;

    return path != nullptr &&
        http_client_accepts_encoding(request.headers, encoding) &&
        file_dispatch_compressed(request2, st, body, encoding, path);
}

static bool
file_check_auto_compressed(Request &request2, const struct stat &st,
                           Istream &body, const char *encoding,
                           const char *path, const char *suffix)
{
    assert(encoding != nullptr);
    assert(path != nullptr);
    assert(suffix != nullptr);
    assert(*suffix == '.');
    assert(suffix[1] != 0);

    const auto &request = request2.request;

    if (!http_client_accepts_encoding(request.headers, encoding))
        return false;

    const char *compressed_path = p_strcat(&request2.pool, path, suffix,
                                           nullptr);

    return file_dispatch_compressed(request2, st, body, encoding,
                                    compressed_path);
}

void
file_callback(Request &request2, const FileAddress &address)
{
    request2.handler.file.address = &address;

    const auto &request = request2.request;

    assert(address.path != nullptr);
    assert(address.delegate == nullptr);

    const char *const path = address.path;

    /* check request */

    if (request.method != HTTP_METHOD_HEAD &&
        request.method != HTTP_METHOD_GET &&
        !request2.processor_focus) {
        method_not_allowed(request2, "GET, HEAD");
        return;
    }

    /* open the file */

    struct stat st;
    Istream *body;

    try {
        body = istream_file_stat_new(request2.instance.event_loop,
                                     request2.pool,
                                     path, st);
    } catch (...) {
        request2.LogDispatchError(std::current_exception());
        return;
    }

    /* check file type */

    if (S_ISCHR(st.st_mode)) {
        /* allow character devices, but skip range etc. */
        request2.DispatchResponse(HTTP_STATUS_OK, HttpHeaders(request2.pool),
                                  UnusedIstreamPtr(body));
        return;
    }

    if (!S_ISREG(st.st_mode)) {
        body->CloseUnused();
        request2.DispatchResponse(HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                  "Not a regular file");
        return;
    }

    struct file_request file_request(st.st_size);

    /* request options */

    if (!file_evaluate_request(request2, istream_file_fd(*body), st,
                               file_request)) {
        body->CloseUnused();
        return;
    }

    /* precompressed? */

    if (!request2.compressed &&
        file_request.range.type == HttpRangeRequest::Type::NONE &&
        !request2.IsTransformationEnabled() &&
        (file_check_compressed(request2, st, *body, "deflate",
                               address.deflated) ||
         (address.auto_gzipped &&
          file_check_auto_compressed(request2, st, *body, "gzip",
                                     address.path, ".gz")) ||
         file_check_compressed(request2, st, *body, "gzip",
                               address.gzipped)))
        return;

    /* build the response */

    file_dispatch(request2, st, file_request, body);
}
