/*
 * Caching HTTP responses.  Memcached backend.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-cache-internal.h"
#include "http-cache-choice.h"
#include "memcached-stock.h"
#include "growing-buffer.h"
#include "serialize.h"
#include "sink-impl.h"
#include "strref.h"
#include "strmap.h"
#include "tpool.h"
#include "background.h"

#include <glib.h>

#include <string.h>

enum http_cache_memcached_type {
    TYPE_DOCUMENT = 2,
};

struct http_cache_memcached_request {
    pool_t pool;

    struct memcached_stock *stock;

    pool_t background_pool;
    struct background_manager *background;

    const char *uri;

    struct strmap *request_headers;

    bool in_choice;
    struct http_cache_choice *choice;

    uint32_t header_size;

    union {
        struct memcached_set_extras set;
    } extras;

    union {
        http_cache_memcached_flush_t flush;
        http_cache_memcached_get_t get;
        http_cache_memcached_put_t put;
    } callback;

    void *callback_ctx;

    struct async_operation_ref *async_ref;
};

/*
 * istream handler
 *
 */

/*
static size_t
mcd_value_data(const void *data, size_t length, void *ctx)
{
    struct http_cache_request *request = ctx;

    request->response.length += length;
    if (request->response.length > (size_t)cacheable_size_limit) {
        istream_close(request->response.input);
        return 0;
    }

    growing_buffer_write_buffer(request->response.output, data, length);
    return length;
}

static void
mcd_value_eof(void *ctx)
{
    struct http_cache_request *request = ctx;

    request->response.input = NULL;

    if (request->cache->cache != NULL)
        list_remove(&request->siblings);

    pool_unref(request->pool);
}

static void
mcd_value_abort(void *ctx)
{
    struct http_cache_request *request = ctx;

    cache_log(4, "http_cache: body_abort %s\n", request->url);

    request->response.input = NULL;

    list_remove(&request->siblings);
    pool_unref(request->pool);
}

static const struct istream_handler mcd_value_handler = {
    .data = mcd_value_data,
    .eof = mcd_value_eof,
    .abort = mcd_value_abort,
};
*/

/*
 * Public functions and memcached-client callbacks
 *
 */

static void
http_cache_memcached_flush_callback(enum memcached_response_status status,
                                    G_GNUC_UNUSED const void *extras,
                                    G_GNUC_UNUSED size_t extras_length,
                                    G_GNUC_UNUSED const void *key,
                                    G_GNUC_UNUSED size_t key_length,
                                    istream_t value, void *ctx)
{
    struct http_cache_memcached_request *request = ctx;

    if (value != NULL)
        istream_close(value);

    request->callback.flush(status == MEMCACHED_STATUS_NO_ERROR,
                            request->callback_ctx);
}

void
http_cache_memcached_flush(pool_t pool, struct memcached_stock *stock,
                           http_cache_memcached_flush_t callback,
                           void *callback_ctx,
                           struct async_operation_ref *async_ref)
{
    struct http_cache_memcached_request *request = p_malloc(pool, sizeof(*request));

    request->pool = pool;
    request->callback.flush = callback;
    request->callback_ctx = callback_ctx;

    memcached_stock_invoke(pool, stock, MEMCACHED_OPCODE_FLUSH,
                           NULL, 0,
                           NULL, 0,
                           NULL,
                           http_cache_memcached_flush_callback, request,
                           async_ref);
}

static struct http_cache_document *
mcd_deserialize_document(pool_t pool, struct strref *header,
                         const struct strmap *request_headers)
{
    struct http_cache_document *document;

    document = p_malloc(pool, sizeof(*document));

    http_cache_info_init(&document->info);

    document->info.expires = deserialize_uint64(header);
    document->vary = deserialize_strmap(header, pool);
    document->status = deserialize_uint16(header);
    document->headers = deserialize_strmap(header, pool);

    if (strref_is_null(header) || !http_status_is_valid(document->status))
        return NULL;

    document->info.last_modified =
        strmap_get_checked(document->headers, "last-modified");
    document->info.etag = strmap_get_checked(document->headers, "etag");
    document->info.vary = strmap_get_checked(document->headers, "vary");

    if (!http_cache_document_fits(document, request_headers))
        /* Vary mismatch */
        return NULL;

    return document;
}

static void
http_cache_memcached_get_callback(enum memcached_response_status status,
                                  const void *extras, size_t extras_length,
                                  const void *key, size_t key_length,
                                  istream_t value, void *ctx);

static void
mcd_choice_cleanup_callback(void *ctx)
{
    struct background_job *job = ctx;

    background_manager_remove(job);
}

static void
mcd_choice_get_callback(const char *key, bool unclean, void *ctx)
{
    struct http_cache_memcached_request *request = ctx;

    if (unclean) {
        /* this choice record is unclean - start cleanup as a
           background job */
        pool_t pool = pool_new_linear(request->background_pool,
                                      "http_cache_choice_cleanup", 8192);
        struct background_job *job = p_malloc(pool, sizeof(*job));

        http_cache_choice_cleanup(pool, request->stock, request->uri,
                                  mcd_choice_cleanup_callback, job,
                                  background_job_add(request->background,
                                                     job));
        pool_unref(pool);
    }

    if (key == NULL) {
        request->callback.get(NULL, 0, request->callback_ctx);
        return;
    }

    request->in_choice = true;
    memcached_stock_invoke(request->pool, request->stock, MEMCACHED_OPCODE_GET,
                           NULL, 0,
                           key, strlen(key),
                           NULL,
                           http_cache_memcached_get_callback, request,
                           request->async_ref);
}

static void
http_cache_memcached_header_callback(void *header_ptr, size_t length,
                                     istream_t tail, void *ctx)
{
    struct http_cache_memcached_request *request = ctx;
    struct strref header;
    enum http_cache_memcached_type type;
    struct http_cache_document *document;

    if (tail == NULL) {
        request->callback.get(NULL, 0, request->callback_ctx);
        return;
    }

    strref_set(&header, header_ptr, length);

    type = deserialize_uint32(&header);
    switch (type) {
    case TYPE_DOCUMENT:
        document = mcd_deserialize_document(request->pool, &header,
                                            request->request_headers);
        if (document == NULL) {
            if (request->in_choice)
                break;

            istream_close(tail);

            http_cache_choice_get(request->pool, request->stock,
                                  request->uri, request->request_headers,
                                  mcd_choice_get_callback, request,
                                  request->async_ref);
            return;
        }

        request->callback.get(document, tail, request->callback_ctx);
        return;
    }

    istream_close(tail);
    request->callback.get(NULL, 0, request->callback_ctx);
}

static void
http_cache_memcached_get_callback(enum memcached_response_status status,
                                  G_GNUC_UNUSED const void *extras,
                                  G_GNUC_UNUSED size_t extras_length,
                                  G_GNUC_UNUSED const void *key,
                                  G_GNUC_UNUSED size_t key_length,
                                  istream_t value, void *ctx)
{
    struct http_cache_memcached_request *request = ctx;

    if (status == MEMCACHED_STATUS_KEY_NOT_FOUND && !request->in_choice) {
        if (value != NULL)
            istream_close(value);

        http_cache_choice_get(request->pool, request->stock,
                              request->uri, request->request_headers,
                              mcd_choice_get_callback, request,
                              request->async_ref);
        return;
    }

    if (status != MEMCACHED_STATUS_NO_ERROR || value == NULL) {
        if (value != NULL)
            istream_close(value);

        request->callback.get(NULL, 0, request->callback_ctx);
        return;
    }

    sink_header_new(request->pool, value,
                    http_cache_memcached_header_callback, request,
                    request->async_ref);
}

void
http_cache_memcached_get(pool_t pool, struct memcached_stock *stock,
                         pool_t background_pool,
                         struct background_manager *background,
                         const char *uri, struct strmap *request_headers,
                         http_cache_memcached_get_t callback,
                         void *callback_ctx,
                         struct async_operation_ref *async_ref)
{
    struct http_cache_memcached_request *request = p_malloc(pool, sizeof(*request));
    const char *key;

    request->pool = pool;
    request->stock = stock;
    request->background_pool = background_pool;
    request->background = background;
    request->uri = uri;
    request->request_headers = request_headers;
    request->in_choice = false;
    request->callback.get = callback;
    request->callback_ctx = callback_ctx;
    request->async_ref = async_ref;

    key = http_cache_choice_vary_key(pool, uri, NULL);

    memcached_stock_invoke(pool, stock, MEMCACHED_OPCODE_GET,
                           NULL, 0,
                           key, strlen(key),
                           NULL,
                           http_cache_memcached_get_callback, request,
                           async_ref);
}

static void
mcd_choice_commit_callback(void *ctx)
{
    struct http_cache_memcached_request *request = ctx;

    request->callback.put(request->callback_ctx);
}

static void
http_cache_memcached_put_callback(enum memcached_response_status status,
                                  G_GNUC_UNUSED const void *extras,
                                  G_GNUC_UNUSED size_t extras_length,
                                  G_GNUC_UNUSED const void *key,
                                  G_GNUC_UNUSED size_t key_length,
                                  istream_t value, void *ctx)
{
    struct http_cache_memcached_request *request = ctx;

    if (value != NULL)
        istream_close(value);

    if (status != MEMCACHED_STATUS_NO_ERROR || /* error */
        request->choice == NULL) { /* or no choice entry needed */
        request->callback.put(request->callback_ctx);
        return;
    }

    http_cache_choice_commit(request->choice, request->stock,
                             mcd_choice_commit_callback, request,
                             request->async_ref);
}

void
http_cache_memcached_put(pool_t pool, struct memcached_stock *stock,
                         pool_t background_pool,
                         struct background_manager *background,
                         const char *uri,
                         const struct http_cache_info *info,
                         struct strmap *request_headers,
                         http_status_t status, struct strmap *response_headers,
                         istream_t value,
                         http_cache_memcached_put_t callback, void *callback_ctx,
                         struct async_operation_ref *async_ref)
{
    struct http_cache_memcached_request *request = p_malloc(pool, sizeof(*request));
    struct pool_mark mark;
    struct strmap *vary;
    struct growing_buffer *gb;
    const char *key;

    request->pool = pool;
    request->stock = stock;
    request->background_pool = background_pool;
    request->background = background;
    request->uri = uri;
    request->async_ref = async_ref;

    pool_mark(tpool, &mark);

    vary = info->vary != NULL
        ? http_cache_copy_vary(tpool, info->vary, request_headers)
        : NULL;

    request->choice = vary != NULL
        ? http_cache_choice_prepare(pool, uri, info, vary)
        : NULL;

    key = http_cache_choice_vary_key(pool, uri, vary);

    gb = growing_buffer_new(pool, 1024);

    /* type */
    serialize_uint32(gb, TYPE_DOCUMENT);

    serialize_uint64(gb, info->expires);
    serialize_strmap(gb, vary);

    /* serialize status + response headers */
    serialize_uint16(gb, status);
    serialize_strmap(gb, response_headers);

    request->header_size = g_htonl(growing_buffer_size(gb));

    /* append response body */
    value = istream_cat_new(pool,
                            istream_memory_new(pool, &request->header_size,
                                               sizeof(request->header_size)),
                            growing_buffer_istream(gb), value, NULL);

    request->extras.set.flags = 0;
    request->extras.set.expiration = info->expires > 0
        ? g_htonl(info->expires) : g_htonl(3600);

    request->callback.put = callback;
    request->callback_ctx = callback_ctx;

    pool_rewind(tpool, &mark);

    memcached_stock_invoke(pool, stock,
                           MEMCACHED_OPCODE_SET,
                           &request->extras.set, sizeof(request->extras.set),
                           key, strlen(key),
                           value,
                           http_cache_memcached_put_callback, request,
                           async_ref);
}

static void
mcd_background_callback(G_GNUC_UNUSED enum memcached_response_status status,
                        G_GNUC_UNUSED const void *extras,
                        G_GNUC_UNUSED size_t extras_length,
                        G_GNUC_UNUSED const void *key,
                        G_GNUC_UNUSED size_t key_length,
                        istream_t value, void *ctx)
{
    struct background_job *job = ctx;

    if (value != NULL)
        istream_close(value);

    background_manager_remove(job);
}

static void
mcd_delete_callback(void *ctx)
{
    struct background_job *job = ctx;

    background_manager_remove(job);
}

void
http_cache_memcached_remove_uri(struct memcached_stock *stock,
                                pool_t background_pool,
                                struct background_manager *background,
                                const char *uri)
{
    pool_t pool = pool_new_linear(background_pool,
                                  "http_cache_memcached_remove_uri", 8192);
    struct background_job *job;
    const char *key;

    job = p_malloc(pool, sizeof(*job));
    key = http_cache_choice_vary_key(pool, uri, NULL);
    memcached_stock_invoke(pool, stock,
                           MEMCACHED_OPCODE_DELETE,
                           NULL, 0,
                           key, strlen(key),
                           NULL,
                           mcd_background_callback, job,
                           background_job_add(background, job));

    job = p_malloc(pool, sizeof(*job));
    http_cache_choice_delete(pool, stock, uri,
                             mcd_delete_callback, job,
                             background_job_add(background, job));

    pool_unref(pool);
}
