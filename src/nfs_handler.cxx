/*
 * Serve HTTP requests from a NFS server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "nfs_handler.hxx"
#include "nfs_cache.hxx"
#include "nfs_address.hxx"
#include "file_headers.hxx"
#include "translation/Vary.hxx"
#include "header_writer.hxx"
#include "generate_response.hxx"
#include "GrowingBuffer.hxx"
#include "request.hxx"
#include "bp_instance.hxx"
#include "http_headers.hxx"
#include "http_server/Request.hxx"
#include "pool.hxx"

#include <glib.h>

#include <assert.h>
#include <sys/stat.h>
#include <unistd.h>

static void
nfs_handler_error(GError *error, void *ctx)
{
    auto &request2 = *(Request *)ctx;

    response_dispatch_error(request2, error);
    g_error_free(error);
}

/*
 * nfs_cache_handler
 *
 */

static void
nfs_handler_cache_response(NfsCacheHandle &handle,
                           const struct stat &st, void *ctx)
{
    auto &request2 = *(Request *)ctx;
    struct pool &pool = request2.pool;
    const TranslateResponse *const tr = request2.translate.response;

    struct file_request file_request(st.st_size);
    if (!file_evaluate_request(request2, -1, st, file_request))
        return;

    const char *override_content_type = request2.translate.content_type;
    if (override_content_type == nullptr)
        override_content_type = request2.translate.address.GetNfs().content_type;

    HttpHeaders headers(pool);
    GrowingBuffer &headers2 = headers.GetBuffer();
    header_write(headers2, "cache-control", "max-age=60");

    file_response_headers(headers2,
                          override_content_type,
                          -1, st,
                          tr->expires_relative,
                          request2.IsProcessorEnabled(),
                          request2.IsProcessorFirst());
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

    Istream *body;
    if (no_body)
        body = NULL;
    else
        body = nfs_cache_handle_open(pool, handle,
                                     file_request.range.skip,
                                     file_request.range.size);

    response_dispatch(request2, status, std::move(headers), body);
}

static constexpr NfsCacheHandler nfs_handler_cache_handler = {
    .response = nfs_handler_cache_response,
    .error = nfs_handler_error,
};

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
                      nfs_handler_cache_handler, &request2,
                      request2.cancel_ptr);
}
