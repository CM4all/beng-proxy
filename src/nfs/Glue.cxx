/*
 * High level NFS client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Glue.hxx"
#include "Cache.hxx"
#include "http_response.hxx"
#include "static_headers.hxx"
#include "strmap.hxx"
#include "pool.hxx"
#include "GException.hxx"

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
        handler.InvokeError(ToGError(ep));
    }
};

void
NfsRequest::OnNfsCacheResponse(NfsCacheHandle &handle, const struct stat &st)
{
    auto headers = static_response_headers(pool, -1, st,
                                           content_type);
    headers.Add("cache-control", "max-age=60");

    Istream *body = nfs_cache_handle_open(pool, handle, 0, st.st_size);

    // TODO: handle revalidation etc.
    handler.InvokeResponse(HTTP_STATUS_OK, std::move(headers), body);
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
