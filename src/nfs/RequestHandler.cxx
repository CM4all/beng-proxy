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

#include "RequestHandler.hxx"
#include "Cache.hxx"
#include "Address.hxx"
#include "translation/Vary.hxx"
#include "header_writer.hxx"
#include "GrowingBuffer.hxx"
#include "bp/FileHeaders.hxx"
#include "bp/Request.hxx"
#include "bp/GenerateResponse.hxx"
#include "bp/Instance.hxx"
#include "http_headers.hxx"
#include "http_server/Request.hxx"
#include "pool.hxx"

#include <assert.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * nfs_cache_handler
 *
 */

void
Request::OnNfsCacheResponse(NfsCacheHandle &handle, const struct stat &st)
{
    const TranslateResponse *const tr = translate.response;

    struct file_request file_request(st.st_size);
    if (!file_evaluate_request(*this, -1, st, file_request))
        return;

    const char *override_content_type = translate.content_type;
    if (override_content_type == nullptr)
        override_content_type = translate.address.GetNfs().content_type;

    HttpHeaders headers(pool);
    GrowingBuffer &headers2 = headers.GetBuffer();
    header_write(headers2, "cache-control", "max-age=60");

    file_response_headers(headers2,
                          override_content_type,
                          -1, st,
                          tr->expires_relative,
                          IsProcessorFirst());
    write_translation_vary_header(headers2, *tr);

    http_status_t status = tr->status == 0 ? HTTP_STATUS_OK : tr->status;

    /* generate the Content-Range header */

    header_write(headers2, "accept-ranges", "bytes");

    bool no_body = false;

    switch (file_request.range.type) {
    case HttpRangeRequest::Type::NONE:
        break;

    case HttpRangeRequest::Type::VALID:
        status = HTTP_STATUS_PARTIAL_CONTENT;

        header_write(headers2, "content-range",
                     p_sprintf(&pool, "bytes %lu-%lu/%lu",
                               (unsigned long)file_request.range.skip,
                               (unsigned long)(file_request.range.size - 1),
                               (unsigned long)st.st_size));
        break;

    case HttpRangeRequest::Type::INVALID:
        status = HTTP_STATUS_REQUESTED_RANGE_NOT_SATISFIABLE;

        header_write(headers2, "content-range",
                     p_sprintf(&pool, "bytes */%lu",
                               (unsigned long)st.st_size));

        no_body = true;
        break;
    }

    Istream *response_body;
    if (no_body)
        response_body = nullptr;
    else
        response_body = nfs_cache_handle_open(pool, handle,
                                              file_request.range.skip,
                                              file_request.range.size);

    DispatchResponse(status, std::move(headers), response_body);
}

void
Request::OnNfsCacheError(std::exception_ptr ep)
{
    LogDispatchError(ep);
}

/*
 * public
 *
 */

void
nfs_handler(Request &request2)
{
    const auto &request = request2.request;
    struct pool &pool = request2.pool;

    const auto &address = request2.translate.address.GetNfs();
    assert(address.server != NULL);
    assert(address.export_name != NULL);
    assert(address.path != NULL);

    /* check request */

    if (request.method != HTTP_METHOD_HEAD &&
        request.method != HTTP_METHOD_GET &&
        !request2.processor_focus) {
        method_not_allowed(request2, "GET, HEAD");
        return;
    }

    /* run the delegate helper */

    nfs_cache_request(pool, *request2.instance.nfs_cache,
                      address.server, address.export_name, address.path,
                      request2, request2.cancel_ptr);
}
