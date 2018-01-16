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

#include "Glue.hxx"
#include "Cache.hxx"
#include "HttpResponseHandler.hxx"
#include "static_headers.hxx"
#include "strmap.hxx"
#include "pool/pool.hxx"
#include "istream/UnusedPtr.hxx"

#include <sys/stat.h>

struct NfsRequest final : NfsCacheHandler {
    struct pool &pool;

    const char *const path;
    const char *const content_type;

    HttpResponseHandler &handler;

    NfsRequest(struct pool &_pool, const char *_path,
               const char *_content_type,
               HttpResponseHandler &_handler)
        :pool(_pool), path(_path), content_type(_content_type),
         handler(_handler) {
    }

    /* virtual methods from NfsCacheHandler */
    void OnNfsCacheResponse(NfsCacheHandle &handle,
                            const struct stat &st) override;

    void OnNfsCacheError(std::exception_ptr ep) override {
        handler.InvokeError(ep);
    }
};

void
NfsRequest::OnNfsCacheResponse(NfsCacheHandle &handle, const struct stat &st)
{
    auto headers = static_response_headers(pool, -1, st,
                                           content_type);
    headers.Add("cache-control", "max-age=60");

    // TODO: handle revalidation etc.
    handler.InvokeResponse(HTTP_STATUS_OK, std::move(headers),
                           nfs_cache_handle_open(pool, handle, 0, st.st_size));
}

/*
 * constructor
 *
 */

void
nfs_request(struct pool &pool, NfsCache &nfs_cache,
            const char *server, const char *export_name, const char *path,
            const char *content_type,
            HttpResponseHandler &handler,
            CancellablePointer &cancel_ptr)
{
    auto r = NewFromPool<NfsRequest>(pool, pool, path, content_type,
                                     handler);

    nfs_cache_request(pool, nfs_cache, server, export_name, path,
                      *r, cancel_ptr);
}
