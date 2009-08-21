/*
 * Caching HTTP responses.  Memcached backend.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-cache-internal.h"
#include "memcached-stock.h"
#include "growing-buffer.h"
#include "serialize.h"
#include "sink-impl.h"
#include "strref.h"
#include "strmap.h"
#include "tpool.h"

#include <glib.h>

#include <string.h>

enum http_cache_memcached_type {
    TYPE_DOCUMENT = 2,
};

struct http_cache_memcached_request {
    pool_t pool;

    const char *uri;

    struct strmap *request_headers;

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
    if (type != TYPE_DOCUMENT) {
        request->callback.get(NULL, 0, request->callback_ctx);
        return;
    }

    document = p_malloc(request->pool, sizeof(*document));

    http_cache_info_init(&document->info);

    document->info.expires = deserialize_uint64(&header);
    document->vary = deserialize_strmap(&header, request->pool);
    document->status = deserialize_uint16(&header);
    document->headers = deserialize_strmap(&header, request->pool);

    if (strref_is_null(&header)) {
        istream_close(tail);
        request->callback.get(NULL, 0, request->callback_ctx);
        return;
    }

    document->info.last_modified =
        strmap_get_checked(document->headers, "last-modified");
    document->info.etag = strmap_get_checked(document->headers, "etag");
    document->info.vary = strmap_get_checked(document->headers, "vary");

    request->callback.get(document, tail, request->callback_ctx);
}

static void
http_cache_memcached_get_callback(enum memcached_response_status status,
                                  G_GNUC_UNUSED const void *key,
                                  G_GNUC_UNUSED size_t key_length,
                                  istream_t value, void *ctx)
{
    struct http_cache_memcached_request *request = ctx;

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
                         const char *uri, struct strmap *request_headers,
                         http_cache_memcached_get_t callback,
                         void *callback_ctx,
                         struct async_operation_ref *async_ref)
{
    struct http_cache_memcached_request *request = p_malloc(pool, sizeof(*request));

    request->pool = pool;
    request->uri = uri;
    request->request_headers = request_headers;
    request->callback.get = callback;
    request->callback_ctx = callback_ctx;
    request->async_ref = async_ref;

    memcached_stock_invoke(pool, stock, MEMCACHED_OPCODE_GET,
                           NULL, 0,
                           uri, strlen(uri),
                           NULL,
                           http_cache_memcached_get_callback, request,
                           async_ref);
}

static void
http_cache_memcached_put_callback(G_GNUC_UNUSED enum memcached_response_status status,
                                  G_GNUC_UNUSED const void *key,
                                  G_GNUC_UNUSED size_t key_length,
                                  istream_t value, void *ctx)
{
    struct http_cache_memcached_request *request = ctx;

    if (value != NULL)
        istream_close(value);

    request->callback.put(request->callback_ctx);
}

void
http_cache_memcached_put(pool_t pool, struct memcached_stock *stock,
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
    struct growing_buffer *gb;

    pool_mark(tpool, &mark);

    gb = growing_buffer_new(pool, 1024);

    /* type */
    serialize_uint32(gb, TYPE_DOCUMENT);

    serialize_uint64(gb, info->expires);

    serialize_strmap(gb, info->vary != NULL
                     ? http_cache_copy_vary(tpool, info->vary, request_headers)
                     : NULL);

    /* serialize status + response headers */
    serialize_uint16(gb, status);
    serialize_strmap(gb, response_headers);

    request->header_size = g_htonl(growing_buffer_length(gb));

    /* append response body */
    value = istream_cat_new(pool,
                            istream_memory_new(pool, &request->header_size,
                                               sizeof(request->header_size)),
                            growing_buffer_istream(gb), value, NULL);

    request->extras.set.flags = 0;
    request->extras.set.expiration = g_htonl(300); /* XXX */

    request->callback.put = callback;
    request->callback_ctx = callback_ctx;

    pool_rewind(tpool, &mark);

    memcached_stock_invoke(pool, stock,
                           MEMCACHED_OPCODE_SET,
                           &request->extras, sizeof(request->extras),
                           uri, strlen(uri),
                           value,
                           http_cache_memcached_put_callback, request,
                           async_ref);
}
