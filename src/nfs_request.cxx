/*
 * High level NFS client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "nfs_request.hxx"
#include "nfs_cache.hxx"
#include "http_response.hxx"
#include "static_headers.hxx"
#include "strmap.hxx"
#include "pool.hxx"

#include <sys/stat.h>

struct nfs_request {
    struct pool &pool;

    const char *const path;
    const char *const content_type;

    struct http_response_handler_ref handler;

    nfs_request(struct pool &_pool, const char *_path,
                const char *_content_type,
                const struct http_response_handler *_handler, void *ctx)
        :pool(_pool), path(_path), content_type(_content_type) {
        handler.Set(*_handler, ctx);
    }
};

static void
nfs_request_error(GError *error, void *ctx)
{
    struct nfs_request *r = (struct nfs_request *)ctx;

    r->handler.InvokeAbort(error);
}

/*
 * NfsCacheHandler
 *
 */

static void
nfs_request_response(NfsCacheHandle &handle,
                     const struct stat &st, void *ctx)
{
    struct nfs_request *r = (struct nfs_request *)ctx;

    auto *headers = strmap_new(&r->pool);
    static_response_headers(r->pool, *headers, -1, st,
                            r->content_type);
    headers->Add("cache-control", "max-age=60");

    Istream *body = nfs_cache_handle_open(r->pool, handle, 0, st.st_size);

    // TODO: handle revalidation etc.
    r->handler.InvokeResponse(HTTP_STATUS_OK, headers, body);
}

static constexpr NfsCacheHandler nfs_request_cache_handler = {
    .response = nfs_request_response,
    .error = nfs_request_error,
};

/*
 * constructor
 *
 */

void
nfs_request(struct pool &pool, NfsCache &nfs_cache,
            const char *server, const char *export_name, const char *path,
            const char *content_type,
            const struct http_response_handler *handler, void *handler_ctx,
            struct async_operation_ref *async_ref)
{
    auto r = NewFromPool<struct nfs_request>(pool, pool, path, content_type,
                                             handler, handler_ctx);

    nfs_cache_request(pool, nfs_cache, server, export_name, path,
                      nfs_request_cache_handler, r,
                      *async_ref);
}
